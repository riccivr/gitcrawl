/* See LICENSE file for copyright and license details. */
#ifndef ENTITY_H
#define ENTITY_H

#include <stddef.h>

size_t decode_html_entity(const char *str, size_t len, char *out_utf8, size_t *consumed);

#endif
