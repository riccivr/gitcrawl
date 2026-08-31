/* See LICENSE file for copyright and license details. */
#define APPROX_IMPLEMENTATION
#include "approx.h"
#include "approx_search.h"
#include "git_plumbing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *
gitcrawl_strcasestr(const char *haystack, const char *needle)
{
	if (!haystack || !needle) return NULL;
	if (!*needle) return haystack;
	for (; *haystack; haystack++) {
		if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
			const char *h = haystack + 1;
			const char *n = needle + 1;
			while (*n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
				h++;
				n++;
			}
			if (!*n) return haystack;
		}
	}
	return NULL;
}

int
approx_match_score(const char *pattern, const char *str)
{
	if (!pattern || !*pattern)
		return 100;
	if (!str || !*str)
		return 0;

	size_t patlen = strlen(pattern);
	size_t strlen_val = strlen(str);
	double sim = approx_sim(pattern, patlen, str, strlen_val, APPROX_ICASE | APPROX_DAMERAU);
	return (int)(sim * 100.0 + 0.5);
}

int
gitcrawl_search_repo(const char *repo_dir, const char *branch, const char *query, int fuzzy)
{
	struct git_tree tree;
	if (git_read_tree(repo_dir, branch ? branch : "refs/heads/archive", &tree) < 0) {
		if (git_read_tree(repo_dir, "HEAD", &tree) < 0) {
			fprintf(stderr, "Error: Could not read tree for branch %s\n", branch ? branch : "archive");
			return -1;
		}
	}

	approx_heap_t *heap = approx_heap_create(50);
	if (!heap) {
		git_tree_free(&tree);
		return -1;
	}

	size_t qlen = query ? strlen(query) : 0;
	size_t match_count = 0;

	for (size_t i = 0; i < tree.count; i++) {
		const char *path = tree.entries[i].path;
		size_t plen = strlen(path);

		if (fuzzy) {
			size_t mstart = 0, mend = 0;
			double score = approx_sim_span(query, qlen, path, plen,
			                               APPROX_ICASE | APPROX_DAMERAU,
			                               &mstart, &mend);
			if (score >= 0.30) {
				approx_heap_push(heap, score, path, NULL, mstart, mend, 1, i);
				match_count++;
			}
		} else {
			if (gitcrawl_strcasestr(path, query)) {
				approx_heap_push(heap, 1.0, path, NULL, 0, 0, 0, i);
				match_count++;
			}
		}
	}

	approx_heap_sort(heap);

	printf("Found %lu matches in %s:\n", (unsigned long)match_count, branch ? branch : "archive");
	for (size_t i = 0; i < heap->size; i++) {
		if (fuzzy) {
			int pct = (int)(heap->items[i].score * 100.0 + 0.5);
			printf("  [%3d] %s\n", pct, heap->items[i].line);
		} else {
			printf("  %s\n", heap->items[i].line);
		}
	}

	approx_heap_free(heap);
	git_tree_free(&tree);
	return 0;
}
