/*
 * approx.h - v1.2.0 - Non-interactive fuzzy string matching and ranking library
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Ricardo Veronese Ricci
 *
 * To create the implementation, define APPROX_IMPLEMENTATION in *one* source file:
 *
 *   #define APPROX_IMPLEMENTATION
 *   #include "approx.h"
 *
 * In all other files, simply include "approx.h" normally.
 *
 * Optional preprocessor configuration:
 *   #define APPROX_STATIC          // Declare functions as static instead of extern
 *   #define APPROX_MALLOC(sz)      // Custom malloc
 *   #define APPROX_FREE(p)         // Custom free
 *   #define APPROX_REALLOC(p, sz)  // Custom realloc
 */

#ifndef APPROX_H
#define APPROX_H

#include <stddef.h>

#ifndef APPROX_VERSION
#define APPROX_VERSION "1.2.0"
#endif

#ifndef APPROX_DEFAULT_THRESHOLD
#define APPROX_DEFAULT_THRESHOLD 0.70
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef APPROX_STATIC
#define APPROXDEF static
#else
#define APPROXDEF extern
#endif

/* Flags for approx_sim and approx_sim_span */
#define APPROX_ICASE    (1 << 0)  /* Case-insensitive matching */
#define APPROX_DAMERAU  (1 << 1)  /* Adjacent transposition tolerance (Damerau-Levenshtein) */
#define APPROX_EXACT    (1 << 2)  /* Full-string comparison instead of substring search */

/* Match item for top-N ranking */
typedef struct approx_match {
	double score;
	char *line;
	char *meta;
	size_t mstart;
	size_t mend;
	int has_span;
	size_t id;
} approx_match_t;

/* Bounded Min-Heap structure */
typedef struct approx_heap {
	approx_match_t *items;
	size_t size;
	size_t cap;
} approx_heap_t;

/* Core similarity functions (returns 0.00 to 1.00) */
APPROXDEF double approx_sim(const char *pat, size_t patlen, const char *str, size_t strlen_val, int flags);

/* Substring similarity with match span tracking */
APPROXDEF double approx_sim_span(const char *pat, size_t patlen, const char *str, size_t strlen_val,
                                 int flags, size_t *mstart, size_t *mend);

/* Field extraction helper (for delimited columns like CSV / TSV / logs) */
APPROXDEF void approx_extract_field(const char *str, size_t strlen_val, char delim, long k,
                                    const char **fstart, size_t *flen);

/* Top-N Bounded Min-Heap for ranking */
APPROXDEF approx_heap_t *approx_heap_create(size_t cap);
APPROXDEF void approx_heap_free(approx_heap_t *h);
APPROXDEF int approx_heap_push(approx_heap_t *h, double score, const char *line, const char *meta,
                               size_t mstart, size_t mend, int has_span, size_t id);
APPROXDEF void approx_heap_sort(approx_heap_t *h);

#ifdef __cplusplus
}
#endif

#endif /* APPROX_H */

#if defined(APPROX_IMPLEMENTATION) && !defined(APPROX_IMPLEMENTATION_INCLUDED)
#define APPROX_IMPLEMENTATION_INCLUDED

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#ifndef APPROX_MALLOC
#define APPROX_MALLOC(sz)      malloc(sz)
#define APPROX_FREE(p)         free(p)
#define APPROX_REALLOC(p, sz)  realloc(p, sz)
#endif

static inline size_t
approx__min3(size_t a, size_t b, size_t c)
{
	size_t m = a < b ? a : b;
	return m < c ? m : c;
}

static inline int
approx__char_eq(char a, char b, int icase)
{
	if (icase)
		return tolower((unsigned char)a) == tolower((unsigned char)b);
	return a == b;
}

static char *
approx__strdup(const char *s)
{
	size_t len;
	char *dup;

	if (!s)
		return NULL;
	len = strlen(s);
	dup = (char *)APPROX_MALLOC(len + 1);
	if (dup)
		memcpy(dup, s, len + 1);
	return dup;
}

APPROXDEF void
approx_extract_field(const char *str, size_t strlen_val, char delim, long k,
                     const char **fstart, size_t *flen)
{
	size_t i = 0, field_idx = 1, start = 0;

	if (k <= 0) {
		*fstart = str;
		*flen = strlen_val;
		return;
	}

	if (delim != '\0') {
		while (i <= strlen_val) {
			if (i == strlen_val || str[i] == delim) {
				if (field_idx == (size_t)k) {
					*fstart = str + start;
					*flen = i - start;
					return;
				}
				field_idx++;
				start = i + 1;
			}
			i++;
		}
	} else {
		while (i < strlen_val) {
			while (i < strlen_val && (str[i] == ' ' || str[i] == '\t'))
				i++;
			if (i == strlen_val)
				break;
			start = i;
			while (i < strlen_val && str[i] != ' ' && str[i] != '\t')
				i++;
			if (field_idx == (size_t)k) {
				*fstart = str + start;
				*flen = i - start;
				return;
			}
			field_idx++;
		}
	}

	*fstart = "";
	*flen = 0;
}

APPROXDEF double
approx_sim_span(const char *pat, size_t patlen, const char *str, size_t strlen_val,
                int flags, size_t *mstart, size_t *mend)
{
	size_t buf_a[256], buf_b[256], buf_c[256];
	size_t s_buf_a[256], s_buf_b[256], s_buf_c[256];
	size_t *pprev, *prev, *curr, *tmp;
	size_t *s_pprev, *s_prev, *s_curr, *s_tmp;
	size_t *alloc_a = NULL, *alloc_b = NULL, *alloc_c = NULL;
	size_t *s_alloc_a = NULL, *s_alloc_b = NULL, *s_alloc_c = NULL;
	size_t min_dist, cost;
	size_t best_start = 0, best_end = 0;
	size_t i, j, from_start;
	int icase = (flags & APPROX_ICASE);
	int damerau = (flags & APPROX_DAMERAU);
	int exact = (flags & APPROX_EXACT);
	char c;

	if (exact) {
		if (mstart) *mstart = 0;
		if (mend) *mend = strlen_val > 0 ? strlen_val - 1 : 0;

		if (patlen == 0 && strlen_val == 0)
			return 1.0;
		if (patlen == 0 || strlen_val == 0)
			return 0.0;
	} else {
		if (patlen == 0) {
			if (mstart) *mstart = 0;
			if (mend) *mend = 0;
			return 1.0;
		}
		if (strlen_val == 0) {
			if (mstart) *mstart = 0;
			if (mend) *mend = 0;
			return 0.0;
		}
	}

	if (patlen > SIZE_MAX / sizeof(size_t) - 1)
		return 0.0;

	if (patlen + 1 <= sizeof(buf_a) / sizeof(buf_a[0])) {
		pprev = buf_a;
		prev = buf_b;
		curr = buf_c;
		s_pprev = s_buf_a;
		s_prev = s_buf_b;
		s_curr = s_buf_c;
	} else {
		alloc_a = (size_t *)APPROX_MALLOC((patlen + 1) * sizeof(size_t));
		alloc_b = (size_t *)APPROX_MALLOC((patlen + 1) * sizeof(size_t));
		alloc_c = (size_t *)APPROX_MALLOC((patlen + 1) * sizeof(size_t));
		s_alloc_a = (size_t *)APPROX_MALLOC((patlen + 1) * sizeof(size_t));
		s_alloc_b = (size_t *)APPROX_MALLOC((patlen + 1) * sizeof(size_t));
		s_alloc_c = (size_t *)APPROX_MALLOC((patlen + 1) * sizeof(size_t));
		if (!alloc_a || !alloc_b || !alloc_c || !s_alloc_a || !s_alloc_b || !s_alloc_c) {
			APPROX_FREE(alloc_a); APPROX_FREE(alloc_b); APPROX_FREE(alloc_c);
			APPROX_FREE(s_alloc_a); APPROX_FREE(s_alloc_b); APPROX_FREE(s_alloc_c);
			return 0.0;
		}
		pprev = alloc_a;
		prev = alloc_b;
		curr = alloc_c;
		s_pprev = s_alloc_a;
		s_prev = s_alloc_b;
		s_curr = s_alloc_c;
	}

	for (i = 0; i <= patlen; i++) {
		prev[i] = i;
		pprev[i] = i;
		s_prev[i] = 0;
		s_pprev[i] = 0;
	}

	min_dist = patlen;

	for (j = 0; j < strlen_val; j++) {
		c = str[j];
		curr[0] = exact ? (j + 1) : 0;
		s_curr[0] = j;

		for (i = 1; i <= patlen; i++) {
			cost = approx__char_eq(pat[i - 1], c, icase) ? 0 : 1;

			curr[i] = approx__min3(curr[i - 1] + 1,
			                       prev[i] + 1,
			                       prev[i - 1] + cost);

			if (curr[i] == prev[i - 1] + cost)
				from_start = (i == 1) ? j : s_prev[i - 1];
			else if (curr[i] == curr[i - 1] + 1)
				from_start = (i == 1) ? j : s_curr[i - 1];
			else
				from_start = s_prev[i];

			if (damerau && j > 0 && i > 1 &&
			    approx__char_eq(pat[i - 1], str[j - 1], icase) &&
			    approx__char_eq(pat[i - 2], c, icase)) {
				if (pprev[i - 2] + 1 < curr[i]) {
					curr[i] = pprev[i - 2] + 1;
					from_start = (i == 2) ? (j - 1) : s_pprev[i - 2];
				}
			}

			s_curr[i] = from_start;
		}

		if (!exact) {
			if (curr[patlen] < min_dist || (curr[patlen] == min_dist && min_dist < patlen)) {
				min_dist = curr[patlen];
				best_start = s_curr[patlen];
				best_end = j;
			}
		}

		tmp = pprev;
		pprev = prev;
		prev = curr;
		curr = tmp;

		s_tmp = s_pprev;
		s_pprev = s_prev;
		s_prev = s_curr;
		s_curr = s_tmp;
	}

	if (alloc_a) {
		APPROX_FREE(alloc_a); APPROX_FREE(alloc_b); APPROX_FREE(alloc_c);
		APPROX_FREE(s_alloc_a); APPROX_FREE(s_alloc_b); APPROX_FREE(s_alloc_c);
	}

	if (exact) {
		size_t dist = prev[patlen];
		size_t max_len = patlen > strlen_val ? patlen : strlen_val;
		if (dist >= max_len)
			return 0.0;
		return 1.0 - ((double)dist / (double)max_len);
	}

	if (mstart)
		*mstart = best_start;
	if (mend)
		*mend = best_end;

	if (min_dist >= patlen)
		return 0.0;

	return 1.0 - ((double)min_dist / (double)patlen);
}

APPROXDEF double
approx_sim(const char *pat, size_t patlen, const char *str, size_t strlen_val, int flags)
{
	return approx_sim_span(pat, patlen, str, strlen_val, flags, NULL, NULL);
}

APPROXDEF approx_heap_t *
approx_heap_create(size_t cap)
{
	approx_heap_t *h;

	if (cap == 0 || cap > SIZE_MAX / sizeof(h->items[0]))
		return NULL;

	h = (approx_heap_t *)APPROX_MALLOC(sizeof(*h));
	if (!h)
		return NULL;

	h->items = (approx_match_t *)APPROX_MALLOC(cap * sizeof(h->items[0]));
	if (!h->items) {
		APPROX_FREE(h);
		return NULL;
	}
	memset(h->items, 0, cap * sizeof(h->items[0]));
	h->size = 0;
	h->cap = cap;
	return h;
}

APPROXDEF void
approx_heap_free(approx_heap_t *h)
{
	size_t i;

	if (!h)
		return;

	for (i = 0; i < h->size; i++) {
		APPROX_FREE(h->items[i].line);
		APPROX_FREE(h->items[i].meta);
	}

	APPROX_FREE(h->items);
	APPROX_FREE(h);
}

static void
approx__sift_up(approx_heap_t *h, size_t idx)
{
	approx_match_t tmp;
	size_t parent;

	while (idx > 0) {
		parent = (idx - 1) / 2;
		if (h->items[idx].score < h->items[parent].score) {
			tmp = h->items[idx];
			h->items[idx] = h->items[parent];
			h->items[parent] = tmp;
			idx = parent;
		} else {
			break;
		}
	}
}

static void
approx__sift_down(approx_heap_t *h, size_t idx)
{
	approx_match_t tmp;
	size_t smallest, left, right;

	smallest = idx;
	left = 2 * idx + 1;
	right = 2 * idx + 2;

	if (left < h->size && h->items[left].score < h->items[smallest].score)
		smallest = left;
	if (right < h->size && h->items[right].score < h->items[smallest].score)
		smallest = right;

	if (smallest != idx) {
		tmp = h->items[idx];
		h->items[idx] = h->items[smallest];
		h->items[smallest] = tmp;
		approx__sift_down(h, smallest);
	}
}

APPROXDEF int
approx_heap_push(approx_heap_t *h, double score, const char *line, const char *meta,
                 size_t mstart, size_t mend, int has_span, size_t id)
{
	char *dup_line, *dup_meta = NULL;

	if (!h || h->cap == 0)
		return 0;

	if (h->size < h->cap) {
		dup_line = approx__strdup(line);
		if (!dup_line)
			return 0;
		if (meta) {
			dup_meta = approx__strdup(meta);
			if (!dup_meta) {
				APPROX_FREE(dup_line);
				return 0;
			}
		}
		h->items[h->size].score = score;
		h->items[h->size].line = dup_line;
		h->items[h->size].meta = dup_meta;
		h->items[h->size].mstart = mstart;
		h->items[h->size].mend = mend;
		h->items[h->size].has_span = has_span;
		h->items[h->size].id = id;
		h->size++;
		approx__sift_up(h, h->size - 1);
		return 1;
	} else if (score > h->items[0].score) {
		dup_line = approx__strdup(line);
		if (!dup_line)
			return 0;
		if (meta) {
			dup_meta = approx__strdup(meta);
			if (!dup_meta) {
				APPROX_FREE(dup_line);
				return 0;
			}
		}
		APPROX_FREE(h->items[0].line);
		APPROX_FREE(h->items[0].meta);
		h->items[0].score = score;
		h->items[0].line = dup_line;
		h->items[0].meta = dup_meta;
		h->items[0].mstart = mstart;
		h->items[0].mend = mend;
		h->items[0].has_span = has_span;
		h->items[0].id = id;
		approx__sift_down(h, 0);
		return 1;
	}

	return 0;
}

static int
approx__cmp_desc(const void *a, const void *b)
{
	const approx_match_t *ia = (const approx_match_t *)a;
	const approx_match_t *ib = (const approx_match_t *)b;

	if (ia->score < ib->score)
		return 1;
	if (ia->score > ib->score)
		return -1;
	return (ia->id > ib->id) - (ia->id < ib->id);
}

APPROXDEF void
approx_heap_sort(approx_heap_t *h)
{
	if (!h || h->size == 0)
		return;
	qsort(h->items, h->size, sizeof(h->items[0]), approx__cmp_desc);
}

#endif /* APPROX_IMPLEMENTATION */
