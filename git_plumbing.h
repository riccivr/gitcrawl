/* See LICENSE file for copyright and license details. */
#ifndef GIT_PLUMBING_H
#define GIT_PLUMBING_H

#include <stddef.h>

struct git_tree_entry {
	char mode[16];
	char type[16];
	char sha[64];
	char path[1024];
};

struct git_tree {
	struct git_tree_entry *entries;
	size_t count;
	size_t cap;
};

void git_tree_init(struct git_tree *tree);
void git_tree_free(struct git_tree *tree);

int git_repo_init(const char *repo_dir);
int git_write_blob(const char *repo_dir, const void *data, size_t len, char *out_sha);
int git_read_tree(const char *repo_dir, const char *ref, struct git_tree *out_tree);
int git_get_ref_commit(const char *repo_dir, const char *ref_name, char *out_commit_sha);
int git_create_commit(const char *repo_dir, const char *tree_sha, const char *parent_sha,
                     const char *msg, char *out_commit_sha);
int git_update_ref(const char *repo_dir, const char *ref_name, const char *commit_sha);
int git_show_diff(const char *repo_dir, const char *ref1, const char *ref2, const char *path);
int git_show_log(const char *repo_dir, const char *ref, const char *path, int limit);
char *git_read_file_at_ref(const char *repo_dir, const char *ref, const char *path, size_t *out_len);
int git_run_gc(const char *repo_dir);

/* Fast object store builder using temporary index */
struct git_index_builder {
	char repo_dir[1024];
	char index_file[1024];
};

int git_index_builder_init(struct git_index_builder *b, const char *repo_dir, const char *base_ref);
int git_index_builder_add_blob(struct git_index_builder *b, const char *mode, const char *blob_sha, const char *path);
int git_index_builder_write_tree(struct git_index_builder *b, char *out_tree_sha);
void git_index_builder_free(struct git_index_builder *b);

#endif
