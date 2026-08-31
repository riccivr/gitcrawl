/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include "../gitcrawl.h"

static void
test_url_sharding_properties(void)
{
	printf("  [property] URL sharding normalization & idempotency...\n");

	const char *test_urls[] = {
		"https://example.com/docs/api/v1/users?b=2&a=1&utm_source=twitter",
		"HTTP://EXAMPLE.COM:443/docs/api/v1/users/?a=1&b=2&fbclid=xyz",
		"https://sub.domain.org/path/to/resource.html#section",
		"https://example.com:8080/deep/nested/structure/file.php?z=9&y=8&x=7",
		"https://example.com///multiple///slashes///in///path",
		NULL
	};

	for (int i = 0; test_urls[i]; i++) {
		struct parsed_url p1, p2;
		assert(parse_and_normalize_url(test_urls[i], &p1) == 0);
		assert(parse_and_normalize_url(p1.normalized_url, &p2) == 0);
		assert(strcmp(p1.normalized_url, p2.normalized_url) == 0);

		for (size_t k = 0; k < strlen(p1.host); k++) {
			assert(!isupper((unsigned char)p1.host[k]));
		}

		assert(strstr(p1.normalized_url, "utm_source") == NULL);
		assert(strstr(p1.normalized_url, "fbclid") == NULL);

		struct shard_paths paths;
		assert(generate_shard_paths(&p1, &paths) == 0);
		assert(strlen(paths.sharded_dir) > 0);
		assert(strstr(paths.md_path, "/index.md") != NULL);
		assert(strstr(paths.gz_path, "/index.html.gz") != NULL);
		assert(strstr(paths.json_path, "/metadata.json") != NULL);
	}

	struct parsed_url u1, u2;
	assert(parse_and_normalize_url("https://example.com/page?alpha=1&beta=2&gamma=3", &u1) == 0);
	assert(parse_and_normalize_url("https://example.com/page?gamma=3&alpha=1&beta=2", &u2) == 0);
	assert(strcmp(u1.normalized_url, u2.normalized_url) == 0);
}

static void
test_sanitizer_invariants(void)
{
	printf("  [property] DOM sanitizer security & noise stripping invariants...\n");

	const char *dirty_inputs[] = {
		"<p>Valid</p><script>alert('xss');</script><p>Text</p>",
		"<div>Content<style>body { display:none; }</style><noscript>Enable JS</noscript></div>",
		"<iframe src=\"https://evil.com/frame\"></iframe><p>Article</p>",
		"<form action=\"/submit\"><input type=\"hidden\" name=\"csrf_token\" value=\"12345\"></form>",
		"<svg><circle cx=\"50\" cy=\"50\" r=\"40\"/></svg><p>Real article</p>",
		"<!-- Comment with <script>nested</script> --><h1>Header</h1>",
		NULL
	};

	for (int i = 0; dirty_inputs[i]; i++) {
		size_t clean_len = 0;
		char *clean = sanitize_html(dirty_inputs[i], strlen(dirty_inputs[i]), &clean_len);
		assert(clean != NULL);
		assert(strstr(clean, "<script") == NULL);
		assert(strstr(clean, "</script>") == NULL);
		assert(strstr(clean, "<style") == NULL);
		assert(strstr(clean, "<noscript") == NULL);
		assert(strstr(clean, "<iframe") == NULL);
		assert(strstr(clean, "<svg") == NULL);
		assert(strstr(clean, "csrf_token") == NULL);
		free(clean);
	}
}

static void
test_parser_ast_invariants(void)
{
	printf("  [property] HTML-to-Markdown parser preservation & formatting invariants...\n");

	const char *html_docs[] = {
		"<h1>Top Heading</h1><p>First paragraph with <strong>bold</strong> and <em>italic</em>.</p>",
		"<ul><li>Item 1</li><li>Item 2</li><li>Item 3</li></ul>",
		"<pre><code>int main(void) {\n    return 0;\n}</code></pre>",
		"<blockquote>Quoted wisdom here.</blockquote>",
		"<p>Special entities: &amp; &lt; &gt; &quot; &#39; &copy; &mdash;</p>",
		"<table><tr><th>Col 1</th><th>Col 2</th></tr><tr><td>Val A</td><td>Val B</td></tr></table>",
		NULL
	};

	for (int i = 0; html_docs[i]; i++) {
		size_t md_len = 0;
		char *md = html_to_markdown(html_docs[i], strlen(html_docs[i]), &md_len);
		assert(md != NULL);
		assert(strlen(md) > 0);

		assert(strstr(md, "<h1>") == NULL);
		assert(strstr(md, "<p>") == NULL);
		assert(strstr(md, "<ul>") == NULL);
		assert(strstr(md, "<li>") == NULL);
		assert(strstr(md, "<table>") == NULL);

		if (i == 0) {
			assert(strstr(md, "# Top Heading") != NULL);
			assert(strstr(md, "**bold**") != NULL);
			assert(strstr(md, "*italic*") != NULL);
		} else if (i == 1) {
			assert(strstr(md, "- Item 1") != NULL);
			assert(strstr(md, "- Item 2") != NULL);
		} else if (i == 2) {
			assert(strstr(md, "```") != NULL);
			assert(strstr(md, "int main(void)") != NULL);
		} else if (i == 3) {
			assert(strstr(md, "> Quoted wisdom here.") != NULL);
		} else if (i == 4) {
			assert(strstr(md, "&") != NULL);
			assert(strstr(md, "<") != NULL);
			assert(strstr(md, ">") != NULL);
			assert(strstr(md, "\"") != NULL);
		}
		free(md);
	}
}

static void
test_fuzzy_search_scoring_invariants(void)
{
	printf("  [property] Fuzzy search scoring & ranking invariants...\n");

	int exact_score = approx_match_score("submitting-patches", "submitting-patches");
	assert(exact_score >= 100);

	int prefix_score = approx_match_score("submitting", "submitting-patches");
	int scattered_score = approx_match_score("sbpt", "submitting-patches");
	assert(prefix_score >= scattered_score);
	assert(scattered_score > 0);

	int zero_score = approx_match_score("xyz12345", "submitting-patches");
	assert(zero_score == 0);

	int score_upper = approx_match_score("SUBMIT", "submitting-patches");
	int score_lower = approx_match_score("submit", "submitting-patches");
	assert(score_upper == score_lower);
}

int
main(void)
{
	printf("Running Property-Based Invariant Tests...\n");
	test_url_sharding_properties();
	test_sanitizer_invariants();
	test_parser_ast_invariants();
	test_fuzzy_search_scoring_invariants();
	printf("Property-Based Invariant Tests passed successfully!\n");
	return 0;
}
