/* See LICENSE file for copyright and license details. */
#ifndef STRBUF_H
#define STRBUF_H

#include <stddef.h>
#include <stdarg.h>

struct strbuf {
	char *buf;
	size_t len;
	size_t cap;
};

void strbuf_init(struct strbuf *sb, size_t initial_cap);
void strbuf_grow(struct strbuf *sb, size_t extra);
void strbuf_append_len(struct strbuf *sb, const char *data, size_t len);
void strbuf_append_str(struct strbuf *sb, const char *str);
void strbuf_append_char(struct strbuf *sb, char c);
void strbuf_printf(struct strbuf *sb, const char *fmt, ...);
void strbuf_vprintf(struct strbuf *sb, const char *fmt, va_list ap);
void strbuf_reset(struct strbuf *sb);
void strbuf_free(struct strbuf *sb);
char *strbuf_detach(struct strbuf *sb, size_t *out_len);

#endif
