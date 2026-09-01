#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../robots.h"

int
main(void)
{
	printf("Running test_robots...\n");

	const char *txt =
		"User-agent: *\n"
		"Disallow: /secret\n"
		"Disallow: /tmp\n"
		"Crawl-delay: 1.5\n"
		"\n"
		"User-agent: gitcrawl\n"
		"Disallow: /private\n";

	struct robots_rules r;
	robots_rules_init(&r);
	assert(robots_parse(&r, txt, strlen(txt)) == 0);
	assert(robots_allowed(&r, "/") == 1);
	assert(robots_allowed(&r, "/docs/page") == 1);
	assert(robots_allowed(&r, "/secret") == 0);
	assert(robots_allowed(&r, "/secret/file") == 0);
	assert(robots_allowed(&r, "/private") == 0);
	assert(r.crawl_delay_ms == 1500);
	robots_rules_free(&r);

	printf("test_robots passed!\n");
	return 0;
}
