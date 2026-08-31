/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sanitizer.h"
#include "strbuf.h"

static int
str_case_prefix(const char *str, const char *prefix)
{
	while (*prefix) {
		if (tolower((unsigned char)*str) != tolower((unsigned char)*prefix))
			return 0;
		str++;
		prefix++;
	}
	return 1;
}

static const char *
find_closing_tag(const char *p, const char *end, const char *tag)
{
	while (p < end) {
		if (*p == '<' && p + 1 < end && p[1] == '/') {
			if (str_case_prefix(p + 2, tag)) {
				const char *close = strchr(p, '>');
				if (close && close < end)
					return close + 1;
			}
		}
		p++;
	}
	return end;
}

static int
is_volatile_token(const char *name_val)
{
	static const char *noise_keywords[] = {
		"csrf", "_token", "authenticity_token", "nonce", "__VIEWSTATE",
		"__EVENTVALIDATION", "session_id", "crumb", "ad-container",
		"cookie-banner", "optanon-alert-box"
	};
	for (size_t i = 0; i < sizeof(noise_keywords)/sizeof(noise_keywords[0]); i++) {
		if (strstr(name_val, noise_keywords[i]))
			return 1;
	}
	return 0;
}

char *
sanitize_html(const char *raw_html, size_t len, size_t *out_len)
{
	if (!raw_html || len == 0) {
		if (out_len) *out_len = 0;
		return strdup("");
	}

	struct strbuf sb;
	strbuf_init(&sb, len);

	const char *p = raw_html;
	const char *end = raw_html + len;

	while (p < end) {
		if (p + 4 <= end && strncmp(p, "<!--", 4) == 0) {
			const char *comment_end = strstr(p + 4, "-->");
			if (comment_end) {
				p = comment_end + 3;
				continue;
			} else {
				break;
			}
		}

		if (*p == '<') {
			static const char *strip_tags[] = {
				"script", "style", "noscript", "svg", "iframe", "canvas"
			};
			int stripped = 0;
			for (size_t i = 0; i < sizeof(strip_tags)/sizeof(strip_tags[0]); i++) {
				size_t tlen = strlen(strip_tags[i]);
				if (p + 1 + tlen < end &&
				    str_case_prefix(p + 1, strip_tags[i]) &&
				    (isspace((unsigned char)p[1 + tlen]) || p[1 + tlen] == '>' || p[1 + tlen] == '/')) {
					p = find_closing_tag(p, end, strip_tags[i]);
					stripped = 1;
					break;
				}
			}
			if (stripped)
				continue;

			if (p + 6 < end && str_case_prefix(p + 1, "input")) {
				const char *tag_end = strchr(p, '>');
				if (tag_end && tag_end < end) {
					char tag_buf[512] = {0};
					size_t tlen = tag_end - p + 1;
					if (tlen < sizeof(tag_buf)) {
						memcpy(tag_buf, p, tlen);
						for (size_t k = 0; tag_buf[k]; k++)
							tag_buf[k] = (char)tolower((unsigned char)tag_buf[k]);
						if (is_volatile_token(tag_buf)) {
							p = tag_end + 1;
							continue;
						}
					}
				}
			}

			const char *tag_end = strchr(p, '>');
			if (tag_end && tag_end < end) {
				size_t tag_len = tag_end - p + 1;
				strbuf_append_len(&sb, p, tag_len);
				p = tag_end + 1;
				continue;
			}
		}

		strbuf_append_char(&sb, *p++);
	}

	return strbuf_detach(&sb, out_len);
}
