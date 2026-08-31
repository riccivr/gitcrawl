/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../gitcrawl.h"

static void
test_malformed_html(void)
{
	printf("  [fuzz] Malformed tags and unclosed structures...\n");

	const char *unclosed = "<div class='outer'><p>Unclosed paragraph<h1>Header<span>Text";
	size_t clean_len = 0, md_len = 0;
	char *clean = sanitize_html(unclosed, strlen(unclosed), &clean_len);
	assert(clean != NULL);
	char *md = html_to_markdown(clean, clean_len, &md_len);
	assert(md != NULL);
	assert(strstr(md, "Unclosed paragraph") != NULL);
	free(clean);
	free(md);

	size_t depth = 1000;
	struct strbuf nested_buf;
	strbuf_init(&nested_buf, depth * 15 + 100);
	for (size_t i = 0; i < depth; i++) {
		strbuf_append_str(&nested_buf, "<div class='nested'>");
	}
	strbuf_append_str(&nested_buf, "Deep Payload");
	for (size_t i = 0; i < depth; i++) {
		strbuf_append_str(&nested_buf, "</div>");
	}
	clean = sanitize_html(nested_buf.buf, nested_buf.len, &clean_len);
	assert(clean != NULL);
	md = html_to_markdown(clean, clean_len, &md_len);
	assert(md != NULL);
	assert(strstr(md, "Deep Payload") != NULL);
	free(clean);
	free(md);
	strbuf_free(&nested_buf);

	const char *binary_html = "<p>Clean\0Hidden</p><script>alert(1)</script><p>Tail</p>";
	clean = sanitize_html(binary_html, strlen(binary_html), &clean_len);
	assert(clean != NULL);
	md = html_to_markdown(clean, clean_len, &md_len);
	assert(md != NULL);
	assert(strstr(md, "Clean") != NULL);
	free(clean);
	free(md);

	struct strbuf giant_buf;
	strbuf_init(&giant_buf, 110000);
	strbuf_append_str(&giant_buf, "<p title='");
	for (int i = 0; i < 100000; i++) {
		strbuf_append_char(&giant_buf, 'A');
	}
	strbuf_append_str(&giant_buf, "'>Giant Attribute Test</p>");
	clean = sanitize_html(giant_buf.buf, giant_buf.len, &clean_len);
	assert(clean != NULL);
	md = html_to_markdown(clean, clean_len, &md_len);
	assert(md != NULL);
	assert(strstr(md, "Giant Attribute Test") != NULL);
	free(clean);
	free(md);
	strbuf_free(&giant_buf);
}

static void
test_malformed_urls(void)
{
	printf("  [fuzz] Malformed and adversarial URLs...\n");

	struct parsed_url p;
	assert(parse_and_normalize_url("://missing-scheme", &p) < 0);
	assert(parse_and_normalize_url("http://", &p) < 0);
	assert(parse_and_normalize_url("", &p) < 0);

	char long_url[9000];
	strcpy(long_url, "https://example.com/api/");
	for (int i = 0; i < 200; i++) {
		strcat(long_url, "subpath/");
	}
	if (parse_and_normalize_url(long_url, &p) == 0) {
		struct shard_paths paths;
		assert(generate_shard_paths(&p, &paths) == 0);
	}
}

int
main(void)
{
	printf("Running Malformed Input & Fuzz Tests...\n");
	test_malformed_html();
	test_malformed_urls();
	printf("Malformed Input & Fuzz Tests passed successfully!\n");
	return 0;
}
