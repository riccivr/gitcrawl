/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../gitcrawl.h"

static double
get_time_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void
bench_dom_sanitizer(void)
{
	printf("--- Benchmark 1: DOM Sanitizer & Noise Stripping ---\n");

	const char *sample_html =
		"<div class='article ad-banner'>"
		"  <h1>Web Archiving Performance with Git</h1>"
		"  <script>var tracker = 'analytics';</script>"
		"  <style>.hidden { display: none; }</style>"
		"  <p>Content-addressable storage models preserve historical pages efficiently.</p>"
		"  <form><input type='hidden' name='csrf_token' value='12345'></form>"
		"  <a href='https://example.com/subpage'>Subpage Link</a>"
		"  <img src='track.png' width='1' height='1'>"
		"</div>";

	size_t iterations = 50000;
	double start = get_time_sec();

	size_t total_bytes = 0;
	size_t input_len = strlen(sample_html);
	for (size_t i = 0; i < iterations; i++) {
		size_t clean_len = 0;
		char *clean = sanitize_html(sample_html, input_len, &clean_len);
		total_bytes += clean_len;
		free(clean);
	}

	double elapsed = get_time_sec() - start;
	double throughput_mb = ((double)input_len * iterations) / (1024.0 * 1024.0 * elapsed);
	double ops_sec = iterations / elapsed;

	printf("  Iterations : %lu\n", (unsigned long)iterations);
	printf("  Elapsed    : %.3f s\n", elapsed);
	printf("  Throughput : %.2f MB/s\n", throughput_mb);
	printf("  Speed      : %.0f ops/sec\n\n", ops_sec);
}

static void
bench_markdown_parser(void)
{
	printf("--- Benchmark 2: HTML-to-Markdown Parser Engine ---\n");

	const char *sample_html =
		"<h1>Introduction to Git Plumbing</h1>"
		"<p>Git operates on objects stored in a directed acyclic graph (DAG).</p>"
		"<h2>Key Concepts</h2>"
		"<ul>"
		"  <li><strong>Blobs</strong>: Store file contents.</li>"
		"  <li><strong>Trees</strong>: Store directory listings.</li>"
		"  <li><strong>Commits</strong>: Point to trees with parent lineage.</li>"
		"</ul>"
		"<pre><code>git hash-object -w file.txt</code></pre>"
		"<blockquote>Simple is better than complex.</blockquote>";

	size_t iterations = 50000;
	double start = get_time_sec();

	size_t total_bytes = 0;
	size_t input_len = strlen(sample_html);
	for (size_t i = 0; i < iterations; i++) {
		size_t md_len = 0;
		char *md = html_to_markdown(sample_html, input_len, &md_len);
		total_bytes += md_len;
		free(md);
	}

	double elapsed = get_time_sec() - start;
	double throughput_mb = ((double)input_len * iterations) / (1024.0 * 1024.0 * elapsed);
	double ops_sec = iterations / elapsed;

	printf("  Iterations : %lu\n", (unsigned long)iterations);
	printf("  Elapsed    : %.3f s\n", elapsed);
	printf("  Throughput : %.2f MB/s\n", throughput_mb);
	printf("  Speed      : %.0f ops/sec\n\n", ops_sec);
}

static void
bench_fuzzy_search(void)
{
	printf("--- Benchmark 3: Fuzzy Path Ranking & Scoring ---\n");

	const char *candidate_paths[] = {
		"archive/docs.kernel.org/process/submitting-patches/index.md",
		"archive/docs.kernel.org/process/coding-style/index.md",
		"archive/docs.kernel.org/process/email-clients/index.md",
		"archive/news.ycombinator.com/item/index.md",
		"archive/github.com/torvalds/linux/index.md",
		"archive/wikipedia.org/wiki/Git/index.md",
		"archive/suckless.org/philosophy/index.md",
		"archive/example.com/api/v1/endpoints/users/index.md"
	};
	size_t path_count = sizeof(candidate_paths) / sizeof(candidate_paths[0]);
	const char *query = "submitting patches";

	size_t iterations = 200000;
	double start = get_time_sec();

	long total_score = 0;
	for (size_t i = 0; i < iterations; i++) {
		const char *path = candidate_paths[i % path_count];
		total_score += approx_match_score(query, path);
	}

	double elapsed = get_time_sec() - start;
	double queries_sec = iterations / elapsed;

	printf("  Iterations : %lu\n", (unsigned long)iterations);
	printf("  Elapsed    : %.3f s\n", elapsed);
	printf("  Speed      : %.0f queries/sec (QPS)\n\n", queries_sec);
}

int
main(void)
{
	printf("====================================================\n");
	printf("      gitcrawl Micro-Benchmark Performance Suite     \n");
	printf("====================================================\n\n");

	bench_dom_sanitizer();
	bench_markdown_parser();
	bench_fuzzy_search();

	printf("====================================================\n");
	printf("All micro-benchmarks completed successfully.\n");
	return 0;
}
