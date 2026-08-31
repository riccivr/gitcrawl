/* See LICENSE file for copyright and license details. */
#ifndef APPROX_SEARCH_H
#define APPROX_SEARCH_H

#include <stddef.h>

int approx_match_score(const char *pattern, const char *str);
int gitcrawl_search_repo(const char *repo_dir, const char *branch, const char *query, int fuzzy);

#endif
