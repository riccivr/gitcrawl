/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "strbuf.h"

void
strbuf_init(struct strbuf *sb, size_t initial_cap)
{
	if (initial_cap < 64)
		initial_cap = 64;
	sb->buf = malloc(initial_cap);
	if (!sb->buf) {
		perror("malloc");
		exit(1);
	}
	sb->buf[0] = '\0';
	sb->len = 0;
	sb->cap = initial_cap;
}

void
strbuf_grow(struct strbuf *sb, size_t extra)
{
	size_t needed = sb->len + extra + 1;
	if (needed > sb->cap) {
		size_t new_cap = sb->cap * 2;
		if (new_cap < needed)
			new_cap = needed + 64;
		char *new_buf = realloc(sb->buf, new_cap);
		if (!new_buf) {
			perror("realloc");
			exit(1);
		}
		sb->buf = new_buf;
		sb->cap = new_cap;
	}
}

void
strbuf_append_len(struct strbuf *sb, const char *data, size_t len)
{
	if (!data || len == 0)
		return;
	strbuf_grow(sb, len);
	memcpy(sb->buf + sb->len, data, len);
	sb->len += len;
	sb->buf[sb->len] = '\0';
}

void
strbuf_append_str(struct strbuf *sb, const char *str)
{
	if (!str)
		return;
	strbuf_append_len(sb, str, strlen(str));
}

void
strbuf_append_char(struct strbuf *sb, char c)
{
	strbuf_grow(sb, 1);
	sb->buf[sb->len++] = c;
	sb->buf[sb->len] = '\0';
}

void
strbuf_vprintf(struct strbuf *sb, const char *fmt, va_list ap)
{
	va_list ap_copy;
	va_copy(ap_copy, ap);
	char dummy[1];
	int needed = vsnprintf(dummy, 0, fmt, ap_copy);
	va_end(ap_copy);

	if (needed < 0)
		return;

	strbuf_grow(sb, (size_t)needed);
	vsnprintf(sb->buf + sb->len, needed + 1, fmt, ap);
	sb->len += (size_t)needed;
	sb->buf[sb->len] = '\0';
}

void
strbuf_printf(struct strbuf *sb, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	strbuf_vprintf(sb, fmt, ap);
	va_end(ap);
}

void
strbuf_reset(struct strbuf *sb)
{
	sb->len = 0;
	if (sb->buf && sb->cap > 0)
		sb->buf[0] = '\0';
}

void
strbuf_free(struct strbuf *sb)
{
	if (sb->buf) {
		free(sb->buf);
		sb->buf = NULL;
	}
	sb->len = 0;
	sb->cap = 0;
}

char *
strbuf_detach(struct strbuf *sb, size_t *out_len)
{
	char *res = sb->buf;
	if (out_len)
		*out_len = sb->len;
	sb->buf = NULL;
	sb->len = 0;
	sb->cap = 0;
	return res;
}
