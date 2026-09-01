/* See LICENSE file for copyright and license details. */
#include "robots.h"
#include "process_utils.h"
#include "strbuf.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void
robots_rules_init(struct robots_rules *r)
{
	memset(r, 0, sizeof(*r));
}

void
robots_rules_free(struct robots_rules *r)
{
	if (r->disallow) {
		for (size_t i = 0; i < r->count; i++)
			free(r->disallow[i]);
		free(r->disallow);
	}
	memset(r, 0, sizeof(*r));
}

static void
robots_add_disallow(struct robots_rules *r, const char *path)
{
	if (!path)
		return;
	if (r->count >= r->cap) {
		size_t ncap = r->cap == 0 ? 8 : r->cap * 2;
		char **n = realloc(r->disallow, ncap * sizeof(char *));
		if (!n)
			return;
		r->disallow = n;
		r->cap = ncap;
	}
	char *dup = strdup(path);
	if (!dup)
		return;
	r->disallow[r->count++] = dup;
}

static void
trim_inplace(char *s)
{
	char *start = s;
	while (*start && isspace((unsigned char)*start))
		start++;
	if (start != s)
		memmove(s, start, strlen(start) + 1);
	size_t n = strlen(s);
	while (n > 0 && isspace((unsigned char)s[n - 1]))
		s[--n] = '\0';
}

int
robots_parse(struct robots_rules *r, const char *text, size_t len)
{
	if (!r || !text)
		return -1;
	r->count = 0;
	r->crawl_delay_ms = 0;

	int in_star = 0;
	int in_gitcrawl = 0;
	int applicable = 0;

	const char *p = text;
	const char *end = text + len;
	char line[1024];

	while (p < end) {
		size_t i = 0;
		while (p < end && *p != '\n' && *p != '\r' && i + 1 < sizeof(line))
			line[i++] = *p++;
		line[i] = '\0';
		while (p < end && (*p == '\n' || *p == '\r'))
			p++;

		char *hash = strchr(line, '#');
		if (hash)
			*hash = '\0';
		trim_inplace(line);
		if (!line[0])
			continue;

		char *colon = strchr(line, ':');
		if (!colon)
			continue;
		*colon = '\0';
		char *key = line;
		char *val = colon + 1;
		trim_inplace(key);
		trim_inplace(val);

		if (strcasecmp(key, "user-agent") == 0) {
			in_star = (strcmp(val, "*") == 0);
			in_gitcrawl = (strcasecmp(val, "gitcrawl") == 0);
			applicable = in_star || in_gitcrawl;
			continue;
		}
		if (!applicable)
			continue;
		if (strcasecmp(key, "disallow") == 0) {
			if (val[0])
				robots_add_disallow(r, val);
		} else if (strcasecmp(key, "crawl-delay") == 0) {
			double sec = atof(val);
			if (sec > 0 && r->crawl_delay_ms == 0)
				r->crawl_delay_ms = (int)(sec * 1000.0);
		}
	}
	r->loaded = 1;
	return 0;
}

int
robots_allowed(const struct robots_rules *r, const char *path)
{
	if (!r || !r->loaded || !path)
		return 1;
	if (!r->count)
		return 1;
	for (size_t i = 0; i < r->count; i++) {
		const char *rule = r->disallow[i];
		if (rule[0] == '\0')
			continue;
		size_t n = strlen(rule);
		if (strncmp(path, rule, n) == 0)
			return 0;
	}
	return 1;
}

int
robots_fetch_for_host(const char *scheme, const char *host, int port, struct robots_rules *out)
{
	robots_rules_init(out);
	if (!scheme || !host)
		return -1;
	snprintf(out->host, sizeof(out->host), "%s", host);

	char url[1024];
	int is_default = (strcmp(scheme, "http") == 0 && port == 80) ||
	                 (strcmp(scheme, "https") == 0 && port == 443);
	if (is_default)
		snprintf(url, sizeof(url), "%s://%s/robots.txt", scheme, host);
	else
		snprintf(url, sizeof(url), "%s://%s:%d/robots.txt", scheme, host, port);

	const char *argv[] = {
		"curl", "-sSL", "--compressed",
		"--connect-timeout", "5",
		"--max-time", "15",
		"-A", "gitcrawl/1.1.0 (+https://github.com/riccivr/gitcrawl)",
		url,
		NULL
	};
	struct strbuf body;
	strbuf_init(&body, 1024);
	int status = run_cmd_argv_timeout(argv, NULL, 0, &body, NULL, NULL, 20000);
	if (status != 0) {
		strbuf_free(&body);
		out->loaded = 1;
		return 0;
	}
	robots_parse(out, body.buf ? body.buf : "", body.len);
	strbuf_free(&body);
	return 0;
}
