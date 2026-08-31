/* See LICENSE file for copyright and license details. */
#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

char *html_to_markdown(const char *html, size_t len, size_t *out_len);

#endif
