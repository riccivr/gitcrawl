/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <zlib.h>
#include "fetcher.h"
#include "sanitizer.h"
#include "parser.h"
#include "strbuf.h"

void
crawl_page_data_init(struct crawl_page_data *page)
{
	memset(page, 0, sizeof(*page));
	page->status_code = 200;
	strcpy(page->content_type, "text/html; charset=utf-8");
}

void
crawl_page_data_free(struct crawl_page_data *page)
{
	if (page->raw_html) free(page->raw_html);
	if (page->sanitized_html) free(page->sanitized_html);
	if (page->markdown) free(page->markdown);
	if (page->metadata_json) free(page->metadata_json);
	if (page->html_gz) free(page->html_gz);
	memset(page, 0, sizeof(*page));
}

static int
gzip_compress(const void *src, size_t src_len, unsigned char **out_gz, size_t *out_len)
{
	z_stream strm;
	memset(&strm, 0, sizeof(strm));

	if (deflateInit2(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
		return -1;

	size_t buf_size = src_len + 512;
	unsigned char *buf = malloc(buf_size);
	if (!buf) {
		deflateEnd(&strm);
		return -1;
	}

	strm.next_in = (Bytef *)src;
	strm.avail_in = (uInt)src_len;
	strm.next_out = buf;
	strm.avail_out = (uInt)buf_size;

	int ret = deflate(&strm, Z_FINISH);
	if (ret != Z_STREAM_END) {
		free(buf);
		deflateEnd(&strm);
		return -1;
	}

	*out_len = strm.total_out;
	*out_gz = buf;
	deflateEnd(&strm);
	return 0;
}

static void
extract_outgoing_links(const char *html, size_t len, const char *base_url, struct crawl_link_queue *queue)
{
	const char *p = html;
	const char *end = html + len;

	while (p < end && queue->count < 512) {
		if (*p == '<' && p + 2 < end && (p[1] == 'a' || p[1] == 'A') && isspace((unsigned char)p[2])) {
			const char *tag_end = strchr(p, '>');
			if (tag_end && tag_end < end) {
				char tag_buf[1024] = {0};
				size_t tlen = tag_end - p + 1;
				if (tlen < sizeof(tag_buf)) {
					memcpy(tag_buf, p, tlen);
					const char *href_pos = strcasestr(tag_buf, "href=");
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
								int exists = 0;
								for (int k = 0; k < queue->count; k++) {
									if (strcmp(queue->urls[k], resolved) == 0) {
										exists = 1;
										break;
									}
								}
								if (!exists) {
									snprintf(queue->urls[queue->count++], sizeof(queue->urls[0]), "%s", resolved);
								}
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
build_metadata_json(struct crawl_page_data *page)
{
	time_t now = time(NULL);
	struct tm *tm_info = gmtime(&now);
	char time_buf[64];
	strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);

	struct strbuf sb;
	strbuf_init(&sb, 1024);

	strbuf_append_str(&sb, "{\n");
	strbuf_printf(&sb, "  \"url\": \"%s\",\n", page->url_info.normalized_url);
	strbuf_printf(&sb, "  \"status_code\": %d,\n", page->status_code);
	strbuf_printf(&sb, "  \"crawled_at\": \"%s\",\n", time_buf);
	strbuf_printf(&sb, "  \"content_type\": \"%s\",\n", page->content_type);
	strbuf_printf(&sb, "  \"etag\": \"%s\",\n", page->etag);
	strbuf_printf(&sb, "  \"last_modified\": \"%s\",\n", page->last_modified);
	strbuf_printf(&sb, "  \"server\": \"%s\",\n", page->server);
	strbuf_printf(&sb, "  \"raw_bytes\": %zu,\n", page->raw_len);
	strbuf_printf(&sb, "  \"markdown_bytes\": %zu\n", page->md_len);
	strbuf_append_str(&sb, "}\n");

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

	char cmd[9000];
	snprintf(cmd, sizeof(cmd),
	         "curl -s -i -L -A \"gitcrawl/1.0 (+https://github.com/riccivr/gitcrawl)\" \"%s\"",
	         page->url_info.normalized_url);

	FILE *fp = popen(cmd, "r");
	if (!fp) return -1;

	struct strbuf raw_resp;
	strbuf_init(&raw_resp, 16384);

	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		strbuf_append_len(&raw_resp, buf, n);
	}
	pclose(fp);

	if (raw_resp.len == 0) {
		strbuf_free(&raw_resp);
		return -1;
	}

	const char *body_start = strstr(raw_resp.buf, "\r\n\r\n");
	if (body_start) {
		body_start += 4;
	} else {
		body_start = strstr(raw_resp.buf, "\n\n");
		if (body_start) body_start += 2;
	}

	if (!body_start) {
		body_start = raw_resp.buf;
	} else {
		size_t hlen = body_start - raw_resp.buf;
		char *hdup = malloc(hlen + 1);
		if (hdup) {
			memcpy(hdup, raw_resp.buf, hlen);
			hdup[hlen] = '\0';

			char *line = strtok(hdup, "\r\n");
			if (line && strncmp(line, "HTTP/", 5) == 0) {
				char *code_p = strchr(line, ' ');
				if (code_p) page->status_code = atoi(code_p + 1);
			}

			while ((line = strtok(NULL, "\r\n")) != NULL) {
				if (strncasecmp(line, "content-type:", 13) == 0) {
					char *val = line + 13;
					while (*val && isspace((unsigned char)*val)) val++;
					snprintf(page->content_type, sizeof(page->content_type), "%s", val);
				} else if (strncasecmp(line, "etag:", 5) == 0) {
					char *val = line + 5;
					while (*val && isspace((unsigned char)*val)) val++;
					snprintf(page->etag, sizeof(page->etag), "%s", val);
				} else if (strncasecmp(line, "last-modified:", 14) == 0) {
					char *val = line + 14;
					while (*val && isspace((unsigned char)*val)) val++;
					snprintf(page->last_modified, sizeof(page->last_modified), "%s", val);
				} else if (strncasecmp(line, "server:", 7) == 0) {
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
	memcpy(page->raw_html, body_start, page->raw_len);
	page->raw_html[page->raw_len] = '\0';
	strbuf_free(&raw_resp);

	gzip_compress(page->raw_html, page->raw_len, &page->html_gz, &page->gz_len);
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

	gzip_compress(page->raw_html, page->raw_len, &page->html_gz, &page->gz_len);
	page->sanitized_html = sanitize_html(page->raw_html, page->raw_len, &page->sanitized_len);
	page->markdown = html_to_markdown(page->sanitized_html, page->sanitized_len, &page->md_len);
	extract_outgoing_links(page->sanitized_html, page->sanitized_len, page->url_info.normalized_url, &page->links);
	build_metadata_json(page);

	return 0;
}
