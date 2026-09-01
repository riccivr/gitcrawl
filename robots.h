/* See LICENSE file for copyright and license details. */
#ifndef ROBOTS_H
#define ROBOTS_H

#include <stddef.h>

struct robots_rules {
	char host[256];
	char **disallow;
	size_t count;
	size_t cap;
	int crawl_delay_ms;
	int loaded;
};

void robots_rules_init(struct robots_rules *r);
void robots_rules_free(struct robots_rules *r);
int robots_parse(struct robots_rules *r, const char *text, size_t len);
int robots_allowed(const struct robots_rules *r, const char *path);
int robots_fetch_for_host(const char *scheme, const char *host, int port, struct robots_rules *out);

#endif
