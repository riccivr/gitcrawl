/* See LICENSE file for copyright and license details. */
#include "fetcher.h"
#include "sanitizer.h"
#include "parser.h"
#include "process_utils.h"
#include "strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#if !defined(NO_ZLIB)
#include <zlib.h>
static int
gzip_compress(const void *src, size_t src_len, unsigned char **out_gz, size_t *out_len)
{
	z_stream strm;
	memset(&strm, 0, sizeof(strm));
	/* 15 + 16 enables gzip format with headers and CRC */
	if (deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		return -1;

	size_t bound = deflateBound(&strm, src_len) + 64;
	unsigned char *buf = malloc(bound);
	if (!buf) {
		deflateEnd(&strm);
		return -1;
	}

	strm.next_in = (Bytef *)src;
	strm.avail_in = (uInt)src_len;
	strm.next_out = buf;
	strm.avail_out = (uInt)bound;

	if (deflate(&strm, Z_FINISH) != Z_STREAM_END) {
		deflateEnd(&strm);
		free(buf);
		return -1;
	}

	*out_len = strm.total_out;
	deflateEnd(&strm);
	*out_gz = buf;
	return 0;
}
#else
static uint32_t
calc_crc32(const unsigned char *buf, size_t len)
{
	uint32_t crc = 0xFFFFFFFF;
	for (size_t i = 0; i < len; i++) {
		crc ^= buf[i];
		for (int k = 0; k < 8; k++) {
			crc = (crc >> 1) ^ (0xEDB88320 & (-(int)(crc & 1)));
		}
	}
	return ~crc;
}

static int
gzip_compress(const void *src, size_t src_len, unsigned char **out_gz, size_t *out_len)
{
	size_t max_blocks = (src_len / 65535) + 1;
	size_t bound = 10 + (max_blocks * 5) + src_len + 8 + 32;
	unsigned char *buf = malloc(bound);
	if (!buf) return -1;

	size_t pos = 0;
	/* Gzip header: ID1, ID2, CM, FLG, MTIME, XFL, OS */
	buf[pos++] = 0x1F;
	buf[pos++] = 0x8B;
	buf[pos++] = 0x08; /* DEFLATE */
	buf[pos++] = 0x00; /* Flags */
	buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x00; buf[pos++] = 0x00; /* MTIME */
	buf[pos++] = 0x02; /* XFL (max compression) */
	buf[pos++] = 0x03; /* OS (Unix) */

	/* Uncompressed Deflate Blocks */
	size_t remaining = src_len;
	const unsigned char *in = (const unsigned char *)src;
	while (remaining > 0 || pos == 10) {
		uint16_t block_len = (remaining > 65535) ? 65535 : (uint16_t)remaining;
		uint8_t is_final = (remaining <= 65535) ? 1 : 0;
		buf[pos++] = is_final; /* BFINAL=is_final, BTYPE=00 (stored) */
		buf[pos++] = (uint8_t)(block_len & 0xFF);
		buf[pos++] = (uint8_t)((block_len >> 8) & 0xFF);
		uint16_t nlen = ~block_len;
		buf[pos++] = (uint8_t)(nlen & 0xFF);
		buf[pos++] = (uint8_t)((nlen >> 8) & 0xFF);

		if (block_len > 0) {
			memcpy(buf + pos, in, block_len);
			pos += block_len;
			in += block_len;
			remaining -= block_len;
		} else {
			break;
		}
	}

	/* Gzip trailer: CRC32 + ISIZE */
	uint32_t crc = calc_crc32((const unsigned char *)src, src_len);
	uint32_t isize = (uint32_t)(src_len & 0xFFFFFFFF);
	buf[pos++] = (uint8_t)(crc & 0xFF);
	buf[pos++] = (uint8_t)((crc >> 8) & 0xFF);
	buf[pos++] = (uint8_t)((crc >> 16) & 0xFF);
	buf[pos++] = (uint8_t)((crc >> 24) & 0xFF);
	buf[pos++] = (uint8_t)(isize & 0xFF);
	buf[pos++] = (uint8_t)((isize >> 8) & 0xFF);
	buf[pos++] = (uint8_t)((isize >> 16) & 0xFF);
	buf[pos++] = (uint8_t)((isize >> 24) & 0xFF);

	*out_len = pos;
	*out_gz = buf;
	return 0;
}
#endif

void
crawl_link_queue_init(struct crawl_link_queue *q)
{
	q->urls = NULL;
	q->count = 0;
	q->cap = 0;
}

void
crawl_link_queue_push(struct crawl_link_queue *q, const char *url)
{
	if (!url || !*url) return;
	if (q->count >= q->cap) {
		size_t ncap = q->cap == 0 ? 32 : q->cap * 2;
		char **nurls = realloc(q->urls, ncap * sizeof(char *));
		if (!nurls) return;
		q->urls = nurls;
		q->cap = ncap;
	}
	char *dup = strdup(url);
	if (dup) {
		q->urls[q->count++] = dup;
	}
}

void
crawl_link_queue_free(struct crawl_link_queue *q)
{
	if (q->urls) {
		for (size_t i = 0; i < q->count; i++) {
			free(q->urls[i]);
		}
		free(q->urls);
		q->urls = NULL;
	}
	q->count = 0;
	q->cap = 0;
}

void
crawl_page_data_init(struct crawl_page_data *page)
{
	memset(page, 0, sizeof(*page));
	page->status_code = 200;
	strcpy(page->content_type, "text/html; charset=utf-8");
	crawl_link_queue_init(&page->links);
}

void
crawl_page_data_free(struct crawl_page_data *page)
{
	if (page->raw_html) free(page->raw_html);
	if (page->sanitized_html) free(page->sanitized_html);
	if (page->markdown) free(page->markdown);
	if (page->metadata_json) free(page->metadata_json);
	if (page->html_gz) free(page->html_gz);
	crawl_link_queue_free(&page->links);
	memset(page, 0, sizeof(*page));
}

static const char *
gitcrawl_ci_find(const char *haystack, const char *needle)
{
	if (!haystack || !needle) return NULL;
	if (!*needle) return haystack;
	for (; *haystack; haystack++) {
		if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
			const char *h = haystack + 1;
			const char *n = needle + 1;
			while (*n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
				h++;
				n++;
			}
			if (!*n) return haystack;
		}
	}
	return NULL;
}

static void
extract_outgoing_links(const char *html, size_t len, const char *base_url, struct crawl_link_queue *queue)
{
	const char *p = html;
	const char *end = html + len;

	while (p < end) {
		if (*p == '<' && p + 2 < end && (p[1] == 'a' || p[1] == 'A') && isspace((unsigned char)p[2])) {
			const char *tag_end = strchr(p, '>');
			if (tag_end && tag_end < end) {
				char tag_buf[1024] = {0};
				size_t tlen = tag_end - p + 1;
				if (tlen < sizeof(tag_buf)) {
					memcpy(tag_buf, p, tlen);
					const char *href_pos = gitcrawl_ci_find(tag_buf, "href=");
					if (href_pos) {
						href_pos += 5;
						while (*href_pos && isspace((unsigned char)*href_pos)) href_pos++;
						char quote = 0;
						if (*href_pos == '"' || *href_pos == '\'') {
							quote = *href_pos++;
						}
						char link_val[1024] = {0};
						size_t pos = 0;
						while (*href_pos && pos + 1 < sizeof(link_val)) {
							if (quote && *href_pos == quote) break;
							if (!quote && (isspace((unsigned char)*href_pos) || *href_pos == '>')) break;
							link_val[pos++] = *href_pos++;
						}
						link_val[pos] = '\0';

						if (link_val[0] && link_val[0] != '#' && strncmp(link_val, "javascript:", 11) != 0 &&
						    strncmp(link_val, "mailto:", 7) != 0) {
							char resolved[8192] = {0};
							if (resolve_url(base_url, link_val, resolved, sizeof(resolved)) == 0) {
								crawl_link_queue_push(queue, resolved);
							}
						}
					}
				}
				p = tag_end + 1;
				continue;
			}
		}
		p++;
	}
}

static void
json_escape_append(struct strbuf *sb, const char *str)
{
	if (!str) return;
	for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
		switch (*p) {
		case '"':  strbuf_append_str(sb, "\\\""); break;
		case '\\': strbuf_append_str(sb, "\\\\"); break;
		case '\b': strbuf_append_str(sb, "\\b"); break;
		case '\f': strbuf_append_str(sb, "\\f"); break;
		case '\n': strbuf_append_str(sb, "\\n"); break;
		case '\r': strbuf_append_str(sb, "\\r"); break;
		case '\t': strbuf_append_str(sb, "\\t"); break;
		default:
			if (*p < 0x20) {
				char hex[8];
				snprintf(hex, sizeof(hex), "\\u%04x", *p);
				strbuf_append_str(sb, hex);
			} else {
				strbuf_append_char(sb, (char)*p);
			}
			break;
		}
	}
}

static void
build_metadata_json(struct crawl_page_data *page)
{
	time_t now = time(NULL);
	struct tm *tm_info = gmtime(&now);
	char time_buf[64];
	strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);

	struct strbuf sb;
	strbuf_init(&sb, 1024);

	strbuf_append_str(&sb, "{\n  \"url\": \"");
	json_escape_append(&sb, page->url_info.normalized_url);
	strbuf_append_str(&sb, "\",\n  \"status_code\": ");
	strbuf_printf(&sb, "%d,\n  \"crawled_at\": \"", page->status_code);
	json_escape_append(&sb, time_buf);
	strbuf_append_str(&sb, "\",\n  \"content_type\": \"");
	json_escape_append(&sb, page->content_type);
	strbuf_append_str(&sb, "\",\n  \"etag\": \"");
	json_escape_append(&sb, page->etag);
	strbuf_append_str(&sb, "\",\n  \"last_modified\": \"");
	json_escape_append(&sb, page->last_modified);
	strbuf_append_str(&sb, "\",\n  \"server\": \"");
	json_escape_append(&sb, page->server);
	strbuf_append_str(&sb, "\",\n  \"raw_bytes\": ");
	strbuf_printf(&sb, "%lu,\n  \"markdown_bytes\": %lu\n}\n",
	              (unsigned long)page->raw_len, (unsigned long)page->md_len);

	page->metadata_json = strbuf_detach(&sb, &page->json_len);
}

int
fetch_url_data(const char *url_str, struct crawl_page_data *page)
{
	crawl_page_data_init(page);
	if (parse_and_normalize_url(url_str, &page->url_info) < 0)
		return -1;
	if (generate_shard_paths(&page->url_info, &page->paths) < 0)
		return -1;

	const char *argv[] = {
		"curl", "-sSL", "--compressed",
		"-A", "gitcrawl/1.1.0 (+https://github.com/riccivr/gitcrawl)",
		"-i", page->url_info.normalized_url,
		NULL
	};

	struct strbuf raw_resp;
	strbuf_init(&raw_resp, 16384);

	int status = run_cmd_argv(argv, NULL, 0, &raw_resp, NULL, NULL);
	if (status != 0 || raw_resp.len == 0) {
		strbuf_free(&raw_resp);
		return -1;
	}

	/* Locate final HTTP header section in case of redirects */
	const char *last_http = raw_resp.buf;
	const char *scan = raw_resp.buf;
	while ((scan = strstr(scan, "HTTP/")) != NULL) {
		last_http = scan;
		scan += 5;
	}

	const char *body_start = strstr(last_http, "\r\n\r\n");
	if (body_start) {
		body_start += 4;
	} else {
		body_start = strstr(last_http, "\n\n");
		if (body_start) body_start += 2;
		else body_start = raw_resp.buf;
	}

	if (body_start > last_http) {
		size_t hlen = body_start - last_http;
		char *hdup = malloc(hlen + 1);
		if (hdup) {
			memcpy(hdup, last_http, hlen);
			hdup[hlen] = '\0';

			char *line = strtok(hdup, "\r\n");
			if (line && strncmp(line, "HTTP/", 5) == 0) {
				char *code_p = strchr(line, ' ');
				if (code_p) page->status_code = atoi(code_p + 1);
			}

			while ((line = strtok(NULL, "\r\n")) != NULL) {
				if (gitcrawl_ci_find(line, "content-type:") == line) {
					char *val = line + 13;
					while (*val && isspace((unsigned char)*val)) val++;
					snprintf(page->content_type, sizeof(page->content_type), "%s", val);
				} else if (gitcrawl_ci_find(line, "etag:") == line) {
					char *val = line + 5;
					while (*val && isspace((unsigned char)*val)) val++;
					snprintf(page->etag, sizeof(page->etag), "%s", val);
				} else if (gitcrawl_ci_find(line, "last-modified:") == line) {
					char *val = line + 14;
					while (*val && isspace((unsigned char)*val)) val++;
					snprintf(page->last_modified, sizeof(page->last_modified), "%s", val);
				} else if (gitcrawl_ci_find(line, "server:") == line) {
					char *val = line + 7;
					while (*val && isspace((unsigned char)*val)) val++;
					snprintf(page->server, sizeof(page->server), "%s", val);
				}
			}
			free(hdup);
		}
	}

	page->raw_len = raw_resp.len - (body_start - raw_resp.buf);
	page->raw_html = malloc(page->raw_len + 1);
	if (!page->raw_html) {
		strbuf_free(&raw_resp);
		return -1;
	}
	memcpy(page->raw_html, body_start, page->raw_len);
	page->raw_html[page->raw_len] = '\0';
	strbuf_free(&raw_resp);

	if (gzip_compress(page->raw_html, page->raw_len, &page->html_gz, &page->gz_len) < 0) {
		page->html_gz = NULL;
		page->gz_len = 0;
	}

	page->sanitized_html = sanitize_html(page->raw_html, page->raw_len, &page->sanitized_len);
	page->markdown = html_to_markdown(page->sanitized_html, page->sanitized_len, &page->md_len);
	extract_outgoing_links(page->sanitized_html, page->sanitized_len, page->url_info.normalized_url, &page->links);
	build_metadata_json(page);

	return 0;
}

int
ingest_stream_data(const char *url_str, FILE *fp, struct crawl_page_data *page)
{
	crawl_page_data_init(page);
	if (parse_and_normalize_url(url_str ? url_str : "https://localhost/index.html", &page->url_info) < 0)
		return -1;
	if (generate_shard_paths(&page->url_info, &page->paths) < 0)
		return -1;

	struct strbuf sb;
	strbuf_init(&sb, 16384);

	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		strbuf_append_len(&sb, buf, n);
	}

	page->raw_html = strbuf_detach(&sb, &page->raw_len);

	if (gzip_compress(page->raw_html, page->raw_len, &page->html_gz, &page->gz_len) < 0) {
		page->html_gz = NULL;
		page->gz_len = 0;
	}

	page->sanitized_html = sanitize_html(page->raw_html, page->raw_len, &page->sanitized_len);
	page->markdown = html_to_markdown(page->sanitized_html, page->sanitized_len, &page->md_len);
	extract_outgoing_links(page->sanitized_html, page->sanitized_len, page->url_info.normalized_url, &page->links);
	build_metadata_json(page);

	return 0;
}
