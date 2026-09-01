/* See LICENSE file for copyright and license details. */
#include "gitcrawl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *argv0;

static void
usage(int status)
{
	fprintf(status == 0 ? stdout : stderr,
		"usage: gitcrawl <command> [options] [arguments]\n\n"
		"Commands:\n"
		"  archive <url>          Archive a single URL or stdin stream directly into Git\n"
		"  crawl <url>            Recursively crawl and snapshot pages into Git\n"
		"  diff <url> [c1] [c2]   Show markdown diff between revisions for a URL\n"
		"  search <query>         Search archived paths and commits with fuzzy ranking\n"
		"  log <url>              Show commit history for an archived URL\n"
		"  show <url>             Display archived markdown, html, or metadata json\n"
		"  list                   List all archived URLs in the Git repository\n"
		"  gc                     Optimize repository packfiles and prune loose objects\n"
		"  version                Display version information\n"
		"  help                   Display this help message\n\n"
		"Options:\n"
		"  -b branch              Target branch (default: archive)\n"
		"  -d repo_dir            Git repository path (default: .)\n"
		"  -m message             Commit message\n"
		"  -l depth               Recursion depth for crawl (default: 1)\n"
		"  -p max_pages           Max pages to crawl (default: 50)\n"
		"  -w delay_ms            Crawl request delay in milliseconds (default: 0)\n"
		"  -s                     Same domain only during crawl (default: off)\n"
		"  -f format              Output format for show (md, html, json)\n"
		"  -i                     Read content from stdin for the specified URL\n"
		"  -z                     Fuzzy search mode\n"
	);
	exit(status);
}

static int
archive_single_page(const char *repo_dir, const char *branch, const char *commit_msg,
                    const struct crawl_page_data *page)
{
	git_repo_init(repo_dir);

	char ref_name[256];
	snprintf(ref_name, sizeof(ref_name), "refs/heads/%s", branch ? branch : "archive");

	char md_sha[65] = {0};
	char gz_sha[65] = {0};
	char json_sha[65] = {0};

	if (git_write_blob(repo_dir, page->markdown, page->md_len, md_sha) < 0) {
		fprintf(stderr, "Error: Failed to write markdown blob\n");
		return -1;
	}
	if (page->html_gz && page->gz_len > 0) {
		if (git_write_blob(repo_dir, page->html_gz, page->gz_len, gz_sha) < 0) {
			fprintf(stderr, "Error: Failed to write gzip blob\n");
			return -1;
		}
	}
	if (git_write_blob(repo_dir, page->metadata_json, page->json_len, json_sha) < 0) {
		fprintf(stderr, "Error: Failed to write metadata blob\n");
		return -1;
	}

	struct git_index_builder b;
	git_index_builder_init(&b, repo_dir, ref_name);
	git_index_builder_add_blob(&b, "100644", md_sha, page->paths.md_path);
	if (gz_sha[0]) {
		git_index_builder_add_blob(&b, "100644", gz_sha, page->paths.gz_path);
	}
	git_index_builder_add_blob(&b, "100644", json_sha, page->paths.json_path);
	for (size_t i = 0; i < page->asset_count; i++) {
		char asset_sha[65] = {0};
		if (git_write_blob(repo_dir, page->assets[i].data, page->assets[i].len, asset_sha) == 0)
			git_index_builder_add_blob(&b, "100644", asset_sha, page->assets[i].rel_path);
	}

	char tree_sha[65] = {0};
	if (git_index_builder_write_tree(&b, tree_sha) < 0) {
		fprintf(stderr, "Error: Failed to write git tree\n");
		git_index_builder_free(&b);
		return -1;
	}
	git_index_builder_free(&b);

	char parent_sha[65] = {0};
	git_get_ref_commit(repo_dir, ref_name, parent_sha);

	char default_msg[9000];
	if (!commit_msg) {
		snprintf(default_msg, sizeof(default_msg), "archive: %s (%lu bytes md)",
		         page->url_info.normalized_url, (unsigned long)page->md_len);
		commit_msg = default_msg;
	}

	char commit_sha[65] = {0};
	if (git_create_commit(repo_dir, tree_sha, parent_sha[0] ? parent_sha : NULL,
	                      commit_msg, commit_sha) < 0) {
		fprintf(stderr, "Error: Failed to create git commit\n");
		return -1;
	}

	if (git_update_ref(repo_dir, ref_name, commit_sha, parent_sha[0] ? parent_sha : NULL) < 0) {
		fprintf(stderr, "Error: Failed to update ref %s\n", ref_name);
		return -1;
	}

	printf("Archived: %s\n", page->url_info.normalized_url);
	printf("  Tree:   %s\n", tree_sha);
	printf("  Commit: %s -> %s\n", commit_sha, ref_name);
	printf("  Paths:  %s\n", page->paths.sharded_dir);
	if (page->asset_count)
		printf("  Assets: %lu\n", (unsigned long)page->asset_count);
	return 0;
}

static int
lookup_stored_etag(const char *repo_dir, const char *branch, const char *json_path, char *out, size_t cap)
{
	out[0] = '\0';
	size_t len = 0;
	char *json = git_read_file_at_ref(repo_dir, branch ? branch : "archive", json_path, &len);
	if (!json)
		return -1;
	const char *key = strstr(json, "\"etag\"");
	if (key) {
		const char *colon = strchr(key, ':');
		if (colon) {
			colon++;
			while (*colon && (*colon == ' ' || *colon == '\t')) colon++;
			if (*colon == '"') {
				colon++;
				size_t i = 0;
				while (colon[i] && colon[i] != '"' && i + 1 < cap)
					i++;
				memcpy(out, colon, i);
				out[i] = '\0';
			}
		}
	}
	free(json);
	return out[0] ? 0 : -1;
}

static int
cmd_archive(int argc, char **argv, const char *repo_dir, const char *branch, const char *commit_msg)
{
	const char *target = NULL;
	int use_stdin = 0;

	ARGBEGIN {
	case 'b': branch = EARGF(usage(1)); break;
	case 'd': repo_dir = EARGF(usage(1)); break;
	case 'm': commit_msg = EARGF(usage(1)); break;
	case 'i':
	case '-': use_stdin = 1; break;
	default: usage(1); break;
	} ARGEND;

	if (argc > 0)
		target = argv[0];

	if (!target && !use_stdin) {
		usage(1);
	}

	struct crawl_page_data page;
	int res;
	if (use_stdin) {
		res = ingest_stream_data(target ? target : "https://stdin.pipe/input.html", stdin, &page);
	} else {
		char etag[128] = {0};
		struct parsed_url preview;
		struct shard_paths paths;
		if (parse_and_normalize_url(target, &preview) == 0 && generate_shard_paths(&preview, &paths) == 0)
			lookup_stored_etag(repo_dir, branch, paths.json_path, etag, sizeof(etag));
		printf("Fetching: %s ...\n", target);
		res = fetch_url_data(target, &page, etag[0] ? etag : NULL);
		if (res == 0 && page.not_modified) {
			printf("Unchanged (HTTP 304): %s\n", page.url_info.normalized_url);
			crawl_page_data_free(&page);
			return 0;
		}
		if (res == 0)
			fetch_page_assets(&page, 1, 16);
	}

	if (res < 0) {
		fprintf(stderr, "Error: Failed to fetch/ingest %s\n", target ? target : "stdin");
		return 1;
	}

	int ret = archive_single_page(repo_dir, branch, commit_msg, &page);
	crawl_page_data_free(&page);
	return ret == 0 ? 0 : 1;
}

struct crawl_queue_item {
	char *url;
	int depth;
};

static int
cmd_crawl(int argc, char **argv, const char *repo_dir, const char *branch)
{
	const char *start_url = NULL;
	int depth = 1;
	int max_pages = 50;
	int same_domain = 0;
	int delay_ms = 0;

	ARGBEGIN {
	case 'b': branch = EARGF(usage(1)); break;
	case 'd': repo_dir = EARGF(usage(1)); break;
	case 'l': depth = atoi(EARGF(usage(1))); break;
	case 'p': max_pages = atoi(EARGF(usage(1))); break;
	case 'w': delay_ms = atoi(EARGF(usage(1))); break;
	case 's': same_domain = 1; break;
	default: usage(1); break;
	} ARGEND;

	if (argc == 0)
		usage(1);
	start_url = argv[0];

	struct parsed_url base_parsed;
	if (parse_and_normalize_url(start_url, &base_parsed) < 0) {
		fprintf(stderr, "Error: Invalid URL %s\n", start_url);
		return 1;
	}

	/* Dynamically allocated queue and visited set */
	size_t q_cap = 64;
	struct crawl_queue_item *queue = malloc(q_cap * sizeof(struct crawl_queue_item));
	if (!queue) return 1;

	size_t q_head = 0;
	size_t q_tail = 0;

	queue[q_tail].url = strdup(base_parsed.normalized_url);
	if (!queue[q_tail].url) {
		free(queue);
		return 1;
	}
	queue[q_tail].depth = 0;
	q_tail++;

	size_t visited_cap = 64;
	char **visited = malloc(visited_cap * sizeof(char *));
	if (!visited) {
		free(queue[0].url);
		free(queue);
		return 1;
	}
	size_t visited_count = 0;

	struct robots_rules robots;
	robots_rules_init(&robots);
	robots_fetch_for_host(base_parsed.scheme, base_parsed.host, base_parsed.port, &robots);
	if (delay_ms == 0 && robots.crawl_delay_ms > 0)
		delay_ms = robots.crawl_delay_ms;

	printf("Starting crawl: %s (depth=%d, max_pages=%d, same_domain=%s)\n",
	       base_parsed.normalized_url, depth, max_pages, same_domain ? "true" : "false");

	while (q_head < q_tail && (int)visited_count < max_pages) {
		char *current_url = queue[q_head].url;
		int cur_d = queue[q_head].depth;
		q_head++;

		int already_visited = 0;
		for (size_t i = 0; i < visited_count; i++) {
			if (strcmp(visited[i], current_url) == 0) {
				already_visited = 1;
				break;
			}
		}
		if (already_visited) {
			free(current_url);
			continue;
		}

		if (visited_count >= visited_cap) {
			size_t ncap = visited_cap * 2;
			char **nvisited = realloc(visited, ncap * sizeof(char *));
			if (!nvisited) {
				fprintf(stderr, "Error: Out of memory growing visited set\n");
				free(current_url);
				break;
			}
			visited = nvisited;
			visited_cap = ncap;
		}
		{
			char *kept = strdup(current_url);
			if (!kept) {
				fprintf(stderr, "Error: Out of memory recording visited URL\n");
				free(current_url);
				break;
			}
			visited[visited_count++] = kept;
		}

		struct parsed_url cur_parsed;
		if (parse_and_normalize_url(current_url, &cur_parsed) == 0) {
			if (strcmp(cur_parsed.host, robots.host) == 0 &&
			    !robots_allowed(&robots, cur_parsed.path)) {
				printf("robots.txt skipped: %s\n", current_url);
				free(current_url);
				continue;
			}
		}

		if (delay_ms > 0 && visited_count > 1) {
			usleep((useconds_t)delay_ms * 1000);
		}

		struct crawl_page_data page;
		printf("[%lu/%d] Crawling (d=%d): %s\n", (unsigned long)visited_count, max_pages, cur_d, current_url);
		char etag[128] = {0};
		struct shard_paths cur_paths;
		if (parse_and_normalize_url(current_url, &cur_parsed) == 0 &&
		    generate_shard_paths(&cur_parsed, &cur_paths) == 0)
			lookup_stored_etag(repo_dir, branch, cur_paths.json_path, etag, sizeof(etag));
		if (fetch_url_data(current_url, &page, etag[0] ? etag : NULL) == 0) {
			if (page.not_modified) {
				printf("Unchanged (HTTP 304): %s\n", current_url);
			} else {
				fetch_page_assets(&page, 1, 8);
				archive_single_page(repo_dir, branch, NULL, &page);
			}

			if (cur_d < depth) {
				for (size_t k = 0; k < page.links.count && (int)q_tail < max_pages * 4; k++) {
					const char *link = page.links.urls[k];
					struct parsed_url link_parsed;
					if (parse_and_normalize_url(link, &link_parsed) == 0) {
						if (same_domain && strcmp(link_parsed.host, base_parsed.host) != 0)
							continue;

						int queued_or_visited = 0;
						for (size_t v = 0; v < visited_count; v++) {
							if (strcmp(visited[v], link_parsed.normalized_url) == 0) {
								queued_or_visited = 1; break;
							}
						}
						if (!queued_or_visited) {
							for (size_t q = q_head; q < q_tail; q++) {
								if (strcmp(queue[q].url, link_parsed.normalized_url) == 0) {
									queued_or_visited = 1; break;
								}
							}
						}
						if (!queued_or_visited) {
							if (q_tail >= q_cap) {
								size_t ncap = q_cap * 2;
								struct crawl_queue_item *nq = realloc(queue, ncap * sizeof(struct crawl_queue_item));
								if (!nq) {
									fprintf(stderr, "Warning: Out of memory growing crawl queue\n");
									break;
								}
								queue = nq;
								q_cap = ncap;
							}
							char *queued = strdup(link_parsed.normalized_url);
							if (!queued) {
								fprintf(stderr, "Warning: Out of memory queueing URL\n");
								break;
							}
							queue[q_tail].url = queued;
							queue[q_tail].depth = cur_d + 1;
							q_tail++;
						}
					}
				}
			}
			crawl_page_data_free(&page);
		}
		free(current_url);
	}

	/* Cleanup remaining queued items */
	for (size_t q = q_head; q < q_tail; q++) {
		free(queue[q].url);
	}
	free(queue);

	for (size_t v = 0; v < visited_count; v++) {
		free(visited[v]);
	}
	free(visited);

	robots_rules_free(&robots);
	printf("Crawl completed: %lu pages archived into %s\n", (unsigned long)visited_count, branch ? branch : "archive");
	return 0;
}

static int
cmd_diff(int argc, char **argv, const char *repo_dir, const char *branch)
{
	const char *target_url = NULL;
	const char *c1 = NULL;
	const char *c2 = NULL;

	ARGBEGIN {
	case 'b': branch = EARGF(usage(1)); break;
	case 'd': repo_dir = EARGF(usage(1)); break;
	default: usage(1); break;
	} ARGEND;

	if (argc == 0) usage(1);
	target_url = argv[0];
	if (argc > 1) c1 = argv[1];
	if (argc > 2) c2 = argv[2];

	struct parsed_url p_url;
	struct shard_paths paths;
	if (parse_and_normalize_url(target_url, &p_url) < 0 || generate_shard_paths(&p_url, &paths) < 0) {
		fprintf(stderr, "Error: Invalid URL %s\n", target_url);
		return 1;
	}

	return git_show_diff(repo_dir, c1 ? c1 : (branch ? branch : "archive"), c2, paths.md_path);
}

static int
cmd_log(int argc, char **argv, const char *repo_dir, const char *branch)
{
	const char *target_url = NULL;
	int limit = 0;

	ARGBEGIN {
	case 'b': branch = EARGF(usage(1)); break;
	case 'd': repo_dir = EARGF(usage(1)); break;
	case 'n': limit = atoi(EARGF(usage(1))); break;
	default: usage(1); break;
	} ARGEND;

	if (argc == 0) usage(1);
	target_url = argv[0];

	struct parsed_url p_url;
	struct shard_paths paths;
	if (parse_and_normalize_url(target_url, &p_url) < 0 || generate_shard_paths(&p_url, &paths) < 0) {
		fprintf(stderr, "Error: Invalid URL %s\n", target_url);
		return 1;
	}

	return git_show_log(repo_dir, branch ? branch : "archive", paths.md_path, limit);
}

static int
cmd_show(int argc, char **argv, const char *repo_dir, const char *branch)
{
	const char *target_url = NULL;
	const char *format = "md";
	const char *commit = NULL;

	ARGBEGIN {
	case 'b': branch = EARGF(usage(1)); break;
	case 'd': repo_dir = EARGF(usage(1)); break;
	case 'f': format = EARGF(usage(1)); break;
	case 'c': commit = EARGF(usage(1)); break;
	default: usage(1); break;
	} ARGEND;

	if (argc == 0) usage(1);
	target_url = argv[0];

	struct parsed_url p_url;
	struct shard_paths paths;
	if (parse_and_normalize_url(target_url, &p_url) < 0 || generate_shard_paths(&p_url, &paths) < 0) {
		fprintf(stderr, "Error: Invalid URL %s\n", target_url);
		return 1;
	}

	const char *path = paths.md_path;
	if (strcmp(format, "html") == 0 || strcmp(format, "gz") == 0)
		path = paths.gz_path;
	else if (strcmp(format, "json") == 0)
		path = paths.json_path;

	size_t len = 0;
	char *data = git_read_file_at_ref(repo_dir, commit ? commit : (branch ? branch : "archive"), path, &len);
	if (!data) {
		fprintf(stderr, "Error: Could not read %s at %s\n", path, commit ? commit : (branch ? branch : "archive"));
		return 1;
	}

	fwrite(data, 1, len, stdout);
	free(data);
	return 0;
}

static int
cmd_search(int argc, char **argv, const char *repo_dir, const char *branch)
{
	const char *query = NULL;
	int fuzzy = 0;

	ARGBEGIN {
	case 'b': branch = EARGF(usage(1)); break;
	case 'd': repo_dir = EARGF(usage(1)); break;
	case 'z': fuzzy = 1; break;
	default: usage(1); break;
	} ARGEND;

	if (argc == 0) usage(1);
	query = argv[0];

	return gitcrawl_search_repo(repo_dir, branch ? branch : "archive", query, fuzzy);
}

static int
cmd_list(int argc, char **argv, const char *repo_dir, const char *branch)
{
	ARGBEGIN {
	case 'b': branch = EARGF(usage(1)); break;
	case 'd': repo_dir = EARGF(usage(1)); break;
	default: usage(1); break;
	} ARGEND;
	(void)argc;

	struct git_tree tree;
	if (git_read_tree(repo_dir, branch ? branch : "archive", &tree) < 0) {
		fprintf(stderr, "Error: Could not read branch %s\n", branch ? branch : "archive");
		return 1;
	}

	printf("Archived endpoints in %s:\n", branch ? branch : "archive");
	for (size_t i = 0; i < tree.count; i++) {
		if (strstr(tree.entries[i].path, "/index.md")) {
			printf("  %s\n", tree.entries[i].path);
		}
	}
	git_tree_free(&tree);
	return 0;
}

static int
cmd_gc(int argc, char **argv, const char *repo_dir)
{
	ARGBEGIN {
	case 'd': repo_dir = EARGF(usage(1)); break;
	default: usage(1); break;
	} ARGEND;
	(void)argc;

	return git_run_gc(repo_dir);
}

int
main(int argc, char **argv)
{
	const char *repo_dir = ".";
	const char *branch = "archive";
	const char *commit_msg = NULL;

	if (argc < 2)
		usage(1);

	if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "version") == 0) {
		puts("gitcrawl-" VERSION);
		return 0;
	}
	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "help") == 0) {
		usage(0);
	}

	const char *cmd = argv[1];
	argv++;
	argc--;

	if (strcmp(cmd, "archive") == 0) {
		return cmd_archive(argc, argv, repo_dir, branch, commit_msg);
	} else if (strcmp(cmd, "crawl") == 0) {
		return cmd_crawl(argc, argv, repo_dir, branch);
	} else if (strcmp(cmd, "diff") == 0) {
		return cmd_diff(argc, argv, repo_dir, branch);
	} else if (strcmp(cmd, "log") == 0) {
		return cmd_log(argc, argv, repo_dir, branch);
	} else if (strcmp(cmd, "show") == 0) {
		return cmd_show(argc, argv, repo_dir, branch);
	} else if (strcmp(cmd, "search") == 0) {
		return cmd_search(argc, argv, repo_dir, branch);
	} else if (strcmp(cmd, "list") == 0) {
		return cmd_list(argc, argv, repo_dir, branch);
	} else if (strcmp(cmd, "gc") == 0) {
		return cmd_gc(argc, argv, repo_dir);
	} else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		usage(1);
	}

	return 0;
}
