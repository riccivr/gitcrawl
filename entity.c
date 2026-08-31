/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "entity.h"

struct named_entity {
	const char *name;
	const char *utf8;
};

static const struct named_entity entities[] = {
	{"amp", "&"},
	{"lt", "<"},
	{"gt", ">"},
	{"quot", "\""},
	{"apos", "'"},
	{"nbsp", " "},
	{"copy", "©"},
	{"reg", "®"},
	{"trade", "™"},
	{"mdash", "—"},
	{"ndash", "–"},
	{"bull", "•"},
	{"hellip", "…"},
	{"prime", "′"},
	{"Prime", "″"},
	{"lsquo", "‘"},
	{"rsquo", "’"},
	{"ldquo", "“"},
	{"rdquo", "”"},
	{"laquo", "«"},
	{"raquo", "»"},
	{"times", "×"},
	{"divide", "÷"},
	{"plusmn", "±"},
	{"deg", "°"},
	{"sect", "§"},
	{"para", "¶"},
	{"euro", "€"},
	{"pound", "£"},
	{"yen", "¥"},
	{"cent", "¢"}
};

static size_t
encode_utf8(uint32_t cp, char *out)
{
	if (cp <= 0x7F) {
		out[0] = (char)cp;
		return 1;
	} else if (cp <= 0x7FF) {
		out[0] = (char)(0xC0 | ((cp >> 6) & 0x1F));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	} else if (cp <= 0xFFFF) {
		out[0] = (char)(0xE0 | ((cp >> 12) & 0x0F));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	} else if (cp <= 0x10FFFF) {
		out[0] = (char)(0xF0 | ((cp >> 18) & 0x07));
		out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[3] = (char)(0x80 | (cp & 0x3F));
		return 4;
	}
	return 0;
}

size_t
decode_html_entity(const char *str, size_t len, char *out_utf8, size_t *consumed)
{
	if (!str || len < 3 || str[0] != '&')
		return 0;

	size_t semi = 1;
	while (semi < len && str[semi] != ';' && semi < 12) {
		semi++;
	}

	if (semi >= len || str[semi] != ';')
		return 0;

	*consumed = semi + 1;

	/* Check numeric entities &#123; or &#x1F; */
	if (str[1] == '#') {
		uint32_t cp = 0;
		if (str[2] == 'x' || str[2] == 'X') {
			for (size_t i = 3; i < semi; i++) {
				if (!isxdigit((unsigned char)str[i]))
					return 0;
			}
			cp = (uint32_t)strtoul(str + 3, NULL, 16);
		} else {
			for (size_t i = 2; i < semi; i++) {
				if (!isdigit((unsigned char)str[i]))
					return 0;
			}
			cp = (uint32_t)strtoul(str + 2, NULL, 10);
		}
		if (cp == 0 || cp > 0x10FFFF)
			return 0;
		return encode_utf8(cp, out_utf8);
	}

	/* Named entities */
	size_t name_len = semi - 1;
	for (size_t i = 0; i < sizeof(entities)/sizeof(entities[0]); i++) {
		if (strlen(entities[i].name) == name_len &&
		    strncmp(str + 1, entities[i].name, name_len) == 0) {
			size_t ulen = strlen(entities[i].utf8);
			memcpy(out_utf8, entities[i].utf8, ulen);
			return ulen;
		}
	}

	return 0;
}
