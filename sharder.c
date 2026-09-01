/* See LICENSE file for copyright and license details. */
#include "sharder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

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
		if (slen == 0 || slen >= sizeof(out->scheme))
			return -1;
		memcpy(out->scheme, p, slen);
		out->scheme[slen] = '\0';
		for (size_t i = 0; out->scheme[i]; i++) {
			if (!isalnum((unsigned char)out->scheme[i]) && out->scheme[i] != '+' &&
			    out->scheme[i] != '-' && out->scheme[i] != '.')
				return -1;
			out->scheme[i] = (char)tolower((unsigned char)out->scheme[i]);
		}
		p = proto_end + 3;
	} else {
		strcpy(out->scheme, "https");
	}

	const char *host_start = p;
	while (*p && *p != '/' && *p != '?' && *p != '#') p++;
	size_t hlen = p - host_start;
	if (hlen == 0 || hlen >= sizeof(out->host))
		return -1;

	memcpy(out->host, host_start, hlen);
	out->host[hlen] = '\0';
	for (size_t i = 0; out->host[i]; i++)
		out->host[i] = (char)tolower((unsigned char)out->host[i]);

	char *colon = strchr(out->host, ':');
	if (colon) {
		*colon = '\0';
		out->port = atoi(colon + 1);
	} else {
		out->port = (strcmp(out->scheme, "http") == 0) ? 80 : 443;
	}

	const char *path_start = p;
	while (*p && *p != '?' && *p != '#') p++;
	size_t plen = p - path_start;
	if (plen == 0) {
		strcpy(out->path, "/");
	} else {
		if (plen >= sizeof(out->path))
			plen = sizeof(out->path) - 1;
		memcpy(out->path, path_start, plen);
		out->path[plen] = '\0';
	}

	if (*p == '?') {
		p++;
		const char *q_start = p;
		while (*p && *p != '#') p++;
		size_t qlen = p - q_start;
		char raw_q[1024] = {0};
		if (qlen < sizeof(raw_q)) {
			memcpy(raw_q, q_start, qlen);
			normalize_query(raw_q, out->query, sizeof(out->query));
		}
	}

	if (out->query[0]) {
		if (out->port == 80 || out->port == 443) {
			snprintf(out->normalized_url, sizeof(out->normalized_url),
			         "%s://%s%s?%s", out->scheme, out->host, out->path, out->query);
		} else {
			snprintf(out->normalized_url, sizeof(out->normalized_url),
			         "%s://%s:%d%s?%s", out->scheme, out->host, out->port, out->path, out->query);
		}
	} else {
		if (out->port == 80 || out->port == 443) {
			snprintf(out->normalized_url, sizeof(out->normalized_url),
			         "%s://%s%s", out->scheme, out->host, out->path);
		} else {
			snprintf(out->normalized_url, sizeof(out->normalized_url),
			         "%s://%s:%d%s", out->scheme, out->host, out->port, out->path);
		}
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
	if (slen >= 5 && strcmp(safe_seg + slen - 5, ".html") == 0) {
		safe_seg[slen - 5] = '\0';
	} else if (slen >= 4 && strcmp(safe_seg + slen - 4, ".htm") == 0) {
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

static void
normalize_relative_path(const char *in, char *out, size_t cap)
{
	if (!in || !*in) {
		snprintf(out, cap, "/");
		return;
	}

	char *segments[128];
	int count = 0;
	char *dup = strdup(in);
	if (!dup) {
		snprintf(out, cap, "%s", in);
		return;
	}

	char *token = strtok(dup, "/");
	while (token) {
		if (strcmp(token, ".") == 0) {
			/* skip */
		} else if (strcmp(token, "..") == 0) {
			if (count > 0)
				count--;
		} else if (*token) {
			if (count < 128)
				segments[count++] = token;
		}
		token = strtok(NULL, "/");
	}

	size_t pos = 0;
	if (pos < cap) out[pos++] = '/';
	for (int i = 0; i < count; i++) {
		size_t slen = strlen(segments[i]);
		if (pos + slen + 2 < cap) {
			if (i > 0) out[pos++] = '/';
			memcpy(out + pos, segments[i], slen);
			pos += slen;
		}
	}
	out[pos] = '\0';
	free(dup);
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
		char norm_path[2048] = {0};
		normalize_relative_path(relative_url, norm_path, sizeof(norm_path));
		snprintf(out_url, out_cap, "%s://%s%s", base.scheme, base.host, norm_path);
		return 0;
	}

	char raw_combined[4096] = {0};
	char base_dir[2048] = {0};
	snprintf(base_dir, sizeof(base_dir), "%s", base.path);
	char *last_slash = strrchr(base_dir, '/');
	if (last_slash) {
		*(last_slash + 1) = '\0';
	} else {
		strcpy(base_dir, "/");
	}

	snprintf(raw_combined, sizeof(raw_combined), "%s%s", base_dir, relative_url);
	char norm_path[2048] = {0};
	normalize_relative_path(raw_combined, norm_path, sizeof(norm_path));

	snprintf(out_url, out_cap, "%s://%s%s", base.scheme, base.host, norm_path);
	return 0;
}
