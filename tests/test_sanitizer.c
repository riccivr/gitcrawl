#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../sanitizer.h"

int main(void) {
	printf("Running test_sanitizer...\n");

	const char *sample =
		"<html><head><script>alert(1);</script><style>body{color:red;}</style></head>"
		"<!-- Comment with <tag> inside -->"
		"<body><h1>Title</h1><input type=\"hidden\" name=\"csrf_token\" value=\"123456\">"
		"<img src=\"https://tracker.com/pixel.gif\" width=\"1\" height=\"1\">"
		"<p>Clean text</p></body></html>";

	size_t out_len = 0;
	char *clean = sanitize_html(sample, strlen(sample), &out_len);
	assert(clean != NULL);
	assert(strstr(clean, "alert(1)") == NULL);
	assert(strstr(clean, "csrf_token") == NULL);
	assert(strstr(clean, "tracker.com/pixel.gif") == NULL);
	assert(strstr(clean, "Comment with") == NULL);
	assert(strstr(clean, "<h1>Title</h1>") != NULL);
	assert(strstr(clean, "<p>Clean text</p>") != NULL);

	free(clean);
	printf("test_sanitizer passed!\n");
	return 0;
}
