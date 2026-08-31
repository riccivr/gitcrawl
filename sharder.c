/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sharder.h"

static int
is_tracking_param(const char *key, size_t len)
{
	static const char *trackers[] = {
		"utm_source", "utm_medium", "utm_campaign", "utm_term", "utm_content",
		"fbclid", "gclid", "ref_src", "_ga", "_gl", "mc_cid", "mc_eid"
	};
	for (size_t i = 0; i < sizeof(trackers)/sizeof(trackers[0]); i++) {
		if (strlen(trackers[i]) == len && strncmp(key, trackers[i], len) == 0)
			return 1;
	}
	return 0;
}

static int
param_cmp(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

static void
normalize_query(const char *raw_q, char *out_q, size_t cap)
{
	out_q[0] = '\0';
	if (!raw_q || !*raw_q)
		return;

	char *dup = strdup(raw_q);
	if (!dup)
		return;

	char *params[64];
	int count = 0;
	char *token = strtok(dup, "&;");
	while (token && count < 64) {
		char *eq = strchr(token, '=');
		size_t klen = eq ? (size_t)(eq - token) : strlen(token);
		if (!is_tracking_param(token, klen)) {
			params[count++] = token;
		}
		token = strtok(NULL, "&;");
	}

	if (count > 1) {
		qsort(params, count, sizeof(char *), param_cmp);
	}

	size_t pos = 0;
	for (int i = 0; i < count; i++) {
		size_t plen = strlen(params[i]);
		if (pos + plen + 2 < cap) {
			if (i > 0)
				out_q[pos++] = '&';
			memcpy(out_q + pos, params[i], plen);
			pos += plen;
			out_q[pos] = '\0';
		}
	}
	free(dup);
}

static void
sanitize_path_segment(const char *in, char *out, size_t cap)
{
	size_t pos = 0;
	for (size_t i = 0; in[i] && pos + 4 < cap; i++) {
		unsigned char c = (unsigned char)in[i];
		if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
		    c == '>' || c == '|' || c == '\\') {
			snprintf(out + pos, 5, "%%%02X", c);
			pos += 3;
		} else {
			out[pos++] = (char)c;
		}
	}
	out[pos] = '\0';
}

int
parse_and_normalize_url(const char *raw_url, struct parsed_url *out)
{
	if (!raw_url || !out)
		return -1;
	memset(out, 0, sizeof(*out));

	const char *p = raw_url;
	while (isspace((unsigned char)*p)) p++;

	const char *proto_end = strstr(p, "://");
	if (proto_end) {
		size_t slen = proto_end - p;
		if (slen >= sizeof(out->scheme))
			slen = sizeof(out->scheme) - 1;
		for (size_t i = 0; i < slen; i++)
			out->scheme[i] = (char)tolower((unsigned char)p[i]);
		out->scheme[slen] = '\0';
		p = proto_end + 3;
	} else {
		strncpy(out->scheme, "https", sizeof(out->scheme) - 1);
	}

	const char *host_start = p;
	const char *host_end = strpbrk(host_start, "/?#");
	if (!host_end)
		host_end = host_start + strlen(host_start);

	char host_buf[256] = {0};
	size_t hlen = host_end - host_start;
	if (hlen >= sizeof(host_buf))
		hlen = sizeof(host_buf) - 1;
	memcpy(host_buf, host_start, hlen);
	host_buf[hlen] = '\0';

	char *colon = strchr(host_buf, ':');
	if (colon) {
		*colon = '\0';
		out->port = atoi(colon + 1);
	} else {
		out->port = (strcmp(out->scheme, "http") == 0) ? 80 : 443;
	}

	for (size_t i = 0; host_buf[i]; i++)
		out->host[i] = (char)tolower((unsigned char)host_buf[i]);

	if (strlen(out->scheme) == 0 || strlen(out->host) == 0)
		return -1;

	p = host_end;
	const char *query_start = strchr(p, '?');
	const char *frag_start = strchr(p, '#');

	size_t path_len;
	if (query_start) {
		path_len = query_start - p;
	} else if (frag_start) {
		path_len = frag_start - p;
	} else {
		path_len = strlen(p);
	}

	if (path_len == 0 || (path_len == 1 && *p != '/')) {
		strcpy(out->path, "/");
	} else {
		if (path_len >= sizeof(out->path))
			path_len = sizeof(out->path) - 1;
		memcpy(out->path, p, path_len);
		out->path[path_len] = '\0';
	}

	if (query_start) {
		const char *q_end = frag_start ? frag_start : query_start + strlen(query_start);
		char raw_q[2048] = {0};
		size_t qlen = q_end - (query_start + 1);
		if (qlen >= sizeof(raw_q))
			qlen = sizeof(raw_q) - 1;
		memcpy(raw_q, query_start + 1, qlen);
		raw_q[qlen] = '\0';
		normalize_query(raw_q, out->query, sizeof(out->query));
	}

	if (out->query[0]) {
		snprintf(out->normalized_url, sizeof(out->normalized_url), "%s://%s%s?%s",
		         out->scheme, out->host, out->path, out->query);
	} else {
		snprintf(out->normalized_url, sizeof(out->normalized_url), "%s://%s%s",
		         out->scheme, out->host, out->path);
	}

	return 0;
}

int
generate_shard_paths(const struct parsed_url *url, struct shard_paths *out)
{
	if (!url || !out)
		return -1;
	memset(out, 0, sizeof(*out));

	char path_clean[1024] = {0};
	const char *p = url->path;
	while (*p == '/') p++;

	char safe_seg[1024] = {0};
	sanitize_path_segment(p, safe_seg, sizeof(safe_seg));

	size_t slen = strlen(safe_seg);
	if (slen > 5 && strcmp(safe_seg + slen - 5, ".html") == 0) {
		safe_seg[slen - 5] = '\0';
	} else if (slen > 4 && strcmp(safe_seg + slen - 4, ".htm") == 0) {
		safe_seg[slen - 4] = '\0';
	}

	slen = strlen(safe_seg);
	while (slen > 0 && safe_seg[slen - 1] == '/') {
		safe_seg[--slen] = '\0';
	}

	if (safe_seg[0] == '\0') {
		strcpy(path_clean, "index");
	} else {
		snprintf(path_clean, sizeof(path_clean), "%s", safe_seg);
	}

	if (url->query[0]) {
		char safe_q[512] = {0};
		sanitize_path_segment(url->query, safe_q, sizeof(safe_q));
		for (size_t i = 0; safe_q[i]; i++) {
			if (safe_q[i] == '&') safe_q[i] = '_';
			if (safe_q[i] == '=') safe_q[i] = '-';
		}
		snprintf(out->sharded_dir, sizeof(out->sharded_dir), "archive/%s/%s/q_%s",
		         url->host, path_clean, safe_q);
	} else {
		snprintf(out->sharded_dir, sizeof(out->sharded_dir), "archive/%s/%s",
		         url->host, path_clean);
	}

	snprintf(out->md_path, sizeof(out->md_path), "%s/index.md", out->sharded_dir);
	snprintf(out->gz_path, sizeof(out->gz_path), "%s/index.html.gz", out->sharded_dir);
	snprintf(out->json_path, sizeof(out->json_path), "%s/metadata.json", out->sharded_dir);

	return 0;
}

int
resolve_url(const char *base_url, const char *relative_url, char *out_url, size_t out_cap)
{
	if (!base_url || !relative_url || !out_url || out_cap == 0)
		return -1;

	while (isspace((unsigned char)*relative_url)) relative_url++;

	if (strncmp(relative_url, "http://", 7) == 0 ||
	    strncmp(relative_url, "https://", 8) == 0) {
		snprintf(out_url, out_cap, "%s", relative_url);
		return 0;
	}

	if (strncmp(relative_url, "//", 2) == 0) {
		snprintf(out_url, out_cap, "https:%s", relative_url);
		return 0;
	}

	struct parsed_url base;
	if (parse_and_normalize_url(base_url, &base) < 0)
		return -1;

	if (relative_url[0] == '/') {
		snprintf(out_url, out_cap, "%s://%s%s", base.scheme, base.host, relative_url);
		return 0;
	}

	char base_dir[2048] = {0};
	snprintf(base_dir, sizeof(base_dir), "%s", base.path);
	char *last_slash = strrchr(base_dir, '/');
	if (last_slash) {
		*(last_slash + 1) = '\0';
	} else {
		strcpy(base_dir, "/");
	}

	snprintf(out_url, out_cap, "%s://%s%s%s", base.scheme, base.host, base_dir, relative_url);
	return 0;
}
