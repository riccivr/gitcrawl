/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "approx_search.h"
#include "git_plumbing.h"

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

	int score = 0;
	const char *p = pattern;
	const char *s = str;
	int consecutive = 0;
	int is_start = 1;

	while (*p && *s) {
		char pc = (char)tolower((unsigned char)*p);
		char sc = (char)tolower((unsigned char)*s);

		if (pc == sc) {
			score += 10;
			if (consecutive > 0)
				score += (consecutive * 5);
			consecutive++;

			if (is_start || *(s - 1) == '/' || *(s - 1) == '_' || *(s - 1) == '-' || *(s - 1) == '.')
				score += 15;
			if (isupper((unsigned char)*s))
				score += 10;

			p++;
		} else {
			consecutive = 0;
		}
		is_start = 0;
		s++;
	}

	return (*p == '\0') ? score : 0;
}

struct search_result {
	char path[1024];
	int score;
};

static int
search_cmp(const void *a, const void *b)
{
	const struct search_result *ra = (const struct search_result *)a;
	const struct search_result *rb = (const struct search_result *)b;
	return rb->score - ra->score;
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

	struct search_result *results = malloc((tree.count + 1) * sizeof(struct search_result));
	if (!results) {
		git_tree_free(&tree);
		return -1;
	}

	size_t match_count = 0;
	for (size_t i = 0; i < tree.count; i++) {
		const char *path = tree.entries[i].path;
		if (fuzzy) {
			int score = approx_match_score(query, path);
			if (score > 0) {
				snprintf(results[match_count].path, sizeof(results[match_count].path), "%s", path);
				results[match_count].score = score;
				match_count++;
			}
		} else {
			if (gitcrawl_strcasestr(path, query)) {
				snprintf(results[match_count].path, sizeof(results[match_count].path), "%s", path);
				results[match_count].score = 100;
				match_count++;
			}
		}
	}

	if (match_count > 1) {
		qsort(results, match_count, sizeof(struct search_result), search_cmp);
	}

	printf("Found %lu matches in %s:\n", (unsigned long)match_count, branch ? branch : "archive");
	for (size_t i = 0; i < match_count && i < 50; i++) {
		if (fuzzy) {
			printf("  [%3d] %s\n", results[i].score, results[i].path);
		} else {
			printf("  %s\n", results[i].path);
		}
	}

	free(results);
	git_tree_free(&tree);
	return 0;
}
