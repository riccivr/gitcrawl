/* See LICENSE file for copyright and license details. */
#include "parser.h"
#include "strbuf.h"
#include "entity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int
ci_equal_n(const char *a, const char *b, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
			return 0;
	}
	return 1;
}

static void
extract_attribute(const char *tag, const char *attr_name, char *out, size_t cap)
{
	out[0] = '\0';
	size_t alen = strlen(attr_name);
	const char *p = tag;

	while (*p) {
		if (ci_equal_n(p, attr_name, alen) &&
		    (isspace((unsigned char)p[alen]) || p[alen] == '=')) {
			p += alen;
			while (*p && isspace((unsigned char)*p)) p++;
			if (*p == '=') {
				p++;
				while (*p && isspace((unsigned char)*p)) p++;
				char quote = 0;
				if (*p == '"' || *p == '\'') {
					quote = *p++;
				}
				size_t pos = 0;
				while (*p && pos + 1 < cap) {
					if (quote && *p == quote) break;
					if (!quote && (isspace((unsigned char)*p) || *p == '>')) break;
					out[pos++] = *p++;
				}
				out[pos] = '\0';
				return;
			}
		}
		p++;
	}
}

char *
html_to_markdown(const char *html, size_t len, size_t *out_len)
{
	if (!html || len == 0) {
		if (out_len) *out_len = 0;
		return strdup("");
	}

	struct strbuf sb;
	strbuf_init(&sb, len);

	const char *p = html;
	const char *end = html + len;

	int in_pre = 0;
	int list_depth = 0;
	int list_type[16] = {0}; /* 0 = ul, 1 = ol */
	int list_count[16] = {0};
	int table_col = 0;
	int table_header = 0;

	char current_link[512] = {0};
	int in_link = 0;

	while (p < end) {
		/* Entity */
		if (*p == '&' && !in_pre) {
			char utf8_buf[8] = {0};
			size_t consumed = 0;
			size_t ulen = decode_html_entity(p, end - p, utf8_buf, &consumed);
			if (ulen > 0) {
				strbuf_append_len(&sb, utf8_buf, ulen);
				p += consumed;
				continue;
			}
		}

		/* Tag */
		if (*p == '<') {
			const char *tag_end = strchr(p, '>');
			if (tag_end && tag_end < end) {
				size_t tlen = tag_end - p + 1;
				char tag_buf[1024] = {0};
				if (tlen < sizeof(tag_buf)) {
					memcpy(tag_buf, p, tlen);
					int is_closing = (tag_buf[1] == '/');
					const char *tag_name = is_closing ? tag_buf + 2 : tag_buf + 1;
					while (*tag_name && isspace((unsigned char)*tag_name)) tag_name++;

					char name_only[32] = {0};
					size_t nlen = 0;
					while (tag_name[nlen] && !isspace((unsigned char)tag_name[nlen]) &&
					       tag_name[nlen] != '>' && tag_name[nlen] != '/' && nlen < 31) {
						name_only[nlen] = (char)tolower((unsigned char)tag_name[nlen]);
						nlen++;
					}
					name_only[nlen] = '\0';

					/* Headings */
					if (name_only[0] == 'h' && name_only[1] >= '1' && name_only[1] <= '6') {
						int level = name_only[1] - '0';
						if (is_closing) {
							strbuf_append_str(&sb, "\n\n");
						} else {
							if (sb.len > 0 && sb.buf[sb.len - 1] != '\n')
								strbuf_append_str(&sb, "\n\n");
							for (int k = 0; k < level; k++)
								strbuf_append_char(&sb, '#');
							strbuf_append_char(&sb, ' ');
						}
					}
					/* Paragraphs, div, section, article */
					else if (strcmp(name_only, "p") == 0 || strcmp(name_only, "div") == 0 ||
					         strcmp(name_only, "section") == 0 || strcmp(name_only, "article") == 0) {
						if (is_closing) {
							strbuf_append_str(&sb, "\n\n");
						} else if (sb.len > 0 && sb.buf[sb.len - 1] != '\n') {
							strbuf_append_str(&sb, "\n\n");
						}
					}
					/* Line break */
					else if (strcmp(name_only, "br") == 0) {
						strbuf_append_str(&sb, "\n");
					}
					/* Bold */
					else if (strcmp(name_only, "strong") == 0 || strcmp(name_only, "b") == 0) {
						strbuf_append_str(&sb, "**");
					}
					/* Italic */
					else if (strcmp(name_only, "em") == 0 || strcmp(name_only, "i") == 0) {
						strbuf_append_str(&sb, "*");
					}
					/* Code & Pre */
					else if (strcmp(name_only, "pre") == 0) {
						if (is_closing) {
							in_pre = 0;
							strbuf_append_str(&sb, "\n```\n\n");
						} else {
							in_pre = 1;
							if (sb.len > 0 && sb.buf[sb.len - 1] != '\n')
								strbuf_append_str(&sb, "\n\n");
							strbuf_append_str(&sb, "```\n");
						}
					}
					else if (strcmp(name_only, "code") == 0) {
						if (!in_pre) {
							strbuf_append_char(&sb, '`');
						}
					}
					/* Blockquote */
					else if (strcmp(name_only, "blockquote") == 0) {
						if (is_closing) {
							strbuf_append_str(&sb, "\n\n");
						} else {
							if (sb.len > 0 && sb.buf[sb.len - 1] != '\n')
								strbuf_append_str(&sb, "\n\n");
							strbuf_append_str(&sb, "> ");
						}
					}
					/* Horizontal Rule */
					else if (strcmp(name_only, "hr") == 0) {
						strbuf_append_str(&sb, "\n\n---\n\n");
					}
					/* Lists */
					else if (strcmp(name_only, "ul") == 0) {
						if (is_closing) {
							if (list_depth > 0) list_depth--;
							strbuf_append_str(&sb, "\n");
						} else {
							if (list_depth < 15) {
								list_type[list_depth] = 0;
								list_count[list_depth] = 0;
								list_depth++;
							}
							if (sb.len > 0 && sb.buf[sb.len - 1] != '\n')
								strbuf_append_str(&sb, "\n");
						}
					}
					else if (strcmp(name_only, "ol") == 0) {
						if (is_closing) {
							if (list_depth > 0) list_depth--;
							strbuf_append_str(&sb, "\n");
						} else {
							if (list_depth < 15) {
								list_type[list_depth] = 1;
								list_count[list_depth] = 1;
								list_depth++;
							}
							if (sb.len > 0 && sb.buf[sb.len - 1] != '\n')
								strbuf_append_str(&sb, "\n");
						}
					}
					else if (strcmp(name_only, "li") == 0) {
						if (is_closing) {
							strbuf_append_str(&sb, "\n");
						} else {
							if (sb.len > 0 && sb.buf[sb.len - 1] != '\n')
								strbuf_append_str(&sb, "\n");
							for (int d = 1; d < list_depth; d++)
								strbuf_append_str(&sb, "  ");
							int cur_idx = list_depth > 0 ? list_depth - 1 : 0;
							if (list_type[cur_idx] == 1) {
								strbuf_printf(&sb, "%d. ", list_count[cur_idx]++);
							} else {
								strbuf_append_str(&sb, "- ");
							}
						}
					}
					/* Links */
					else if (strcmp(name_only, "a") == 0) {
						if (is_closing) {
							if (in_link && current_link[0]) {
								strbuf_printf(&sb, "](%s)", current_link);
								current_link[0] = '\0';
								in_link = 0;
							}
						} else {
							extract_attribute(tag_buf, "href", current_link, sizeof(current_link));
							if (current_link[0]) {
								strbuf_append_char(&sb, '[');
								in_link = 1;
							}
						}
					}
					/* Images */
					else if (strcmp(name_only, "img") == 0) {
						char src[512] = {0};
						char alt[256] = {0};
						extract_attribute(tag_buf, "src", src, sizeof(src));
						extract_attribute(tag_buf, "alt", alt, sizeof(alt));
						if (src[0]) {
							strbuf_printf(&sb, "![%s](%s)", alt, src);
						}
					}
					/* Tables */
					else if (strcmp(name_only, "table") == 0) {
						if (is_closing) {
							strbuf_append_str(&sb, "\n\n");
						} else {
							table_header = 0;
							if (sb.len > 0 && sb.buf[sb.len - 1] != '\n')
								strbuf_append_str(&sb, "\n\n");
						}
					}
					else if (strcmp(name_only, "tr") == 0) {
						if (is_closing) {
							strbuf_append_str(&sb, "|\n");
							if (table_header) {
								for (int c = 0; c < table_col; c++)
									strbuf_append_str(&sb, "|---");
								strbuf_append_str(&sb, "|\n");
								table_header = 0;
							}
							table_col = 0;
						}
					}
					else if (strcmp(name_only, "th") == 0 || strcmp(name_only, "td") == 0) {
						if (!is_closing) {
							strbuf_append_str(&sb, "| ");
							table_col++;
							if (strcmp(name_only, "th") == 0)
								table_header = 1;
						} else {
							strbuf_append_char(&sb, ' ');
						}
					}
				}
				p = tag_end + 1;
				continue;
			}
		}

		/* Text character handling */
		if (in_pre) {
			strbuf_append_char(&sb, *p++);
		} else {
			if (isspace((unsigned char)*p)) {
				if (sb.len > 0 && !isspace((unsigned char)sb.buf[sb.len - 1])) {
					strbuf_append_char(&sb, ' ');
				}
				p++;
			} else {
				strbuf_append_char(&sb, *p++);
			}
		}
	}

	return strbuf_detach(&sb, out_len);
}
