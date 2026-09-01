#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../parser.h"

int main(void) {
	printf("Running test_parser...\n");

	const char *html =
		"<h1>Article Heading</h1>"
		"<p>This is a paragraph with <strong>bold</strong> and <em>italic</em> text and <code>inline code</code>.</p>"
		"<ul><li>Item 1</li><li>Item 2</li></ul>"
		"<ol><li>Step 1</li><li>Step 2</li></ol>"
		"<pre><code>line 1\n  line 2\n</code></pre>"
		"<p><a href=\"https://example.com\">Link Text</a></p>"
		"<table><tr><th>Header A</th><th>Header B</th></tr><tr><td>Row 1</td><td>Row 2</td></tr></table>";

	size_t out_len = 0;
	char *md = html_to_markdown(html, strlen(html), &out_len);
	assert(md != NULL);
	assert(strstr(md, "# Article Heading") != NULL);
	assert(strstr(md, "**bold**") != NULL);
	assert(strstr(md, "*italic*") != NULL);
	assert(strstr(md, "`inline code`") != NULL);
	assert(strstr(md, "- Item 1") != NULL);
	assert(strstr(md, "1. Step 1") != NULL);
	assert(strstr(md, "2. Step 2") != NULL);
	assert(strstr(md, "```\nline 1\n  line 2\n\n```") != NULL);
	assert(strstr(md, "[Link Text](https://example.com)") != NULL);
	assert(strstr(md, "| Header A | Header B |") != NULL);

	free(md);
	printf("test_parser passed!\n");
	return 0;
}
