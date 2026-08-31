#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../sharder.h"

int main(void) {
	printf("Running test_sharder...\n");

	struct parsed_url url;
	int res = parse_and_normalize_url("https://docs.kernel.org/process/submitting-patches.html?utm_source=twitter&sort=asc&b=2&a=1#section", &url);
	assert(res == 0);
	assert(strcmp(url.scheme, "https") == 0);
	assert(strcmp(url.host, "docs.kernel.org") == 0);
	assert(strcmp(url.query, "a=1&b=2&sort=asc") == 0);

	struct shard_paths paths;
	res = generate_shard_paths(&url, &paths);
	assert(res == 0);
	assert(strstr(paths.md_path, "docs.kernel.org/process/submitting-patches") != NULL);
	assert(strstr(paths.md_path, "index.md") != NULL);

	char resolved[1024];
	resolve_url("https://example.com/blog/post.html", "about.html", resolved, sizeof(resolved));
	assert(strcmp(resolved, "https://example.com/blog/about.html") == 0);

	resolve_url("https://example.com/blog/post.html", "/api/v1", resolved, sizeof(resolved));
	assert(strcmp(resolved, "https://example.com/api/v1") == 0);

	printf("test_sharder passed!\n");
	return 0;
}
