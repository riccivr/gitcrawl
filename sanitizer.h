/* See LICENSE file for copyright and license details. */
#ifndef SANITIZER_H
#define SANITIZER_H

#include <stddef.h>

char *sanitize_html(const char *raw_html, size_t len, size_t *out_len);

#endif
