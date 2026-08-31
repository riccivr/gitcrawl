/* See LICENSE file for copyright and license details. */
#ifndef SHARDER_H
#define SHARDER_H

#include <stddef.h>

struct parsed_url {
	char scheme[16];
	char host[256];
	int port;
	char path[2048];
	char query[2048];
	char normalized_url[8192];
};

struct shard_paths {
	char sharded_dir[2048];
	char md_path[4096];
	char gz_path[4096];
	char json_path[4096];
};

int parse_and_normalize_url(const char *raw_url, struct parsed_url *out);
int generate_shard_paths(const struct parsed_url *url, struct shard_paths *out);
int resolve_url(const char *base_url, const char *relative_url, char *out_url, size_t out_cap);

#endif
