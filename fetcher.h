/* See LICENSE file for copyright and license details. */
#ifndef FETCHER_H
#define FETCHER_H

#include <stddef.h>
#include <stdio.h>
#include "sharder.h"

struct crawl_link_queue {
	char **urls;
	size_t count;
	size_t cap;
};

void crawl_link_queue_init(struct crawl_link_queue *q);
void crawl_link_queue_push(struct crawl_link_queue *q, const char *url);
void crawl_link_queue_free(struct crawl_link_queue *q);

struct crawl_asset {
	char rel_path[4096];
	unsigned char *data;
	size_t len;
};

struct crawl_page_data {
	struct parsed_url url_info;
	struct shard_paths paths;
	char *raw_html;
	size_t raw_len;
	char *sanitized_html;
	size_t sanitized_len;
	char *markdown;
	size_t md_len;
	char *metadata_json;
	size_t json_len;
	unsigned char *html_gz;
	size_t gz_len;
	int status_code;
	int not_modified;
	char content_type[128];
	char etag[128];
	char last_modified[128];
	char server[128];
	struct crawl_link_queue links;
	struct crawl_asset *assets;
	size_t asset_count;
	size_t asset_cap;
};

void crawl_page_data_init(struct crawl_page_data *page);
void crawl_page_data_free(struct crawl_page_data *page);

int fetch_url_data(const char *url_str, struct crawl_page_data *page, const char *if_none_match);
int ingest_stream_data(const char *url_str, FILE *fp, struct crawl_page_data *page);
void fetch_page_assets(struct crawl_page_data *page, int same_host_only, size_t max_assets);

#endif
