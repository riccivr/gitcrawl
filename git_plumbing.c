/* See LICENSE file for copyright and license details. */
#include "git_plumbing.h"
#include "process_utils.h"
#include "strbuf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

void
git_tree_init(struct git_tree *tree)
{
	tree->entries = NULL;
	tree->count = 0;
	tree->cap = 0;
}

void
git_tree_free(struct git_tree *tree)
{
	if (tree->entries) {
		free(tree->entries);
		tree->entries = NULL;
	}
	tree->count = 0;
	tree->cap = 0;
}

int
git_repo_init(const char *repo_dir)
{
	const char *dir = (repo_dir && *repo_dir) ? repo_dir : ".";
	char git_path[2048];
	snprintf(git_path, sizeof(git_path), "%s/.git", dir);
	struct stat st;
	if (stat(git_path, &st) == 0)
		return 0;

#if !defined(_WIN32)
	mkdir(dir, 0755);
#else
	mkdir(dir);
#endif

	const char *init_argv[] = {"git", "-C", dir, "init", "--quiet", NULL};
	if (run_cmd_argv(init_argv, NULL, 0, NULL, NULL, NULL) != 0)
		return -1;

	/* Set local fallback identity only if not configured */
	const char *check_name[] = {"git", "-C", dir, "config", "user.name", NULL};
	struct strbuf out, err;
	strbuf_init(&out, 64);
	strbuf_init(&err, 64);
	if (run_cmd_argv(check_name, NULL, 0, &out, &err, NULL) != 0 || out.len == 0) {
		const char *set_name[] = {"git", "-C", dir, "config", "user.name", "gitcrawl", NULL};
		const char *set_email[] = {"git", "-C", dir, "config", "user.email", "gitcrawl@localhost", NULL};
		run_cmd_argv(set_name, NULL, 0, NULL, &err, NULL);
		run_cmd_argv(set_email, NULL, 0, NULL, &err, NULL);
	}
	strbuf_free(&out);
	strbuf_free(&err);

	return 0;
}

int
git_write_blob(const char *repo_dir, const void *data, size_t len, char *out_sha)
{
	const char *dir = (repo_dir && *repo_dir) ? repo_dir : ".";
	const char *argv[] = {"git", "-C", dir, "hash-object", "-w", "--stdin", NULL};
	struct strbuf out;
	strbuf_init(&out, 128);

	int res = run_cmd_argv(argv, data, len, &out, NULL, NULL);
	if (res != 0) {
		strbuf_free(&out);
		return -1;
	}

	while (out.len > 0 && (out.buf[out.len - 1] == '\n' || out.buf[out.len - 1] == '\r' || out.buf[out.len - 1] == ' '))
		out.buf[--out.len] = '\0';

	if (out.len == 40 || out.len == 64) {
		snprintf(out_sha, 65, "%s", out.buf);
		strbuf_free(&out);
		return 0;
	}

	strbuf_free(&out);
	return -1;
}

int
git_index_builder_init(struct git_index_builder *b, const char *repo_dir, const char *base_ref)
{
	const char *dir = (repo_dir && *repo_dir) ? repo_dir : ".";
	snprintf(b->repo_dir, sizeof(b->repo_dir), "%s", dir);

	static int seq = 0;
#if defined(_WIN32)
	const char *tmp_dir = getenv("TEMP");
	if (!tmp_dir) tmp_dir = getenv("TMP");
	if (!tmp_dir) tmp_dir = ".";
	snprintf(b->index_file, sizeof(b->index_file), "%s/gitcrawl_idx_%d_%d", tmp_dir, (int)getpid(), seq++);
#else
	snprintf(b->index_file, sizeof(b->index_file), "/tmp/gitcrawl_idx_%d_%d", (int)getpid(), seq++);
#endif
	unlink(b->index_file);

	if (base_ref && *base_ref) {
		char commit[65] = {0};
		if (git_get_ref_commit(dir, base_ref, commit) == 0) {
			char env_idx[2048];
			snprintf(env_idx, sizeof(env_idx), "GIT_INDEX_FILE=%s", b->index_file);
			const char *env[] = {env_idx, NULL};
			const char *argv[] = {"git", "-C", dir, "read-tree", commit, NULL};
			run_cmd_argv(argv, NULL, 0, NULL, NULL, env);
		}
	}
	return 0;
}

int
git_index_builder_add_blob(struct git_index_builder *b, const char *mode, const char *blob_sha, const char *path)
{
	char env_idx[2048];
	snprintf(env_idx, sizeof(env_idx), "GIT_INDEX_FILE=%s", b->index_file);
	const char *env[] = {env_idx, NULL};
	const char *argv[] = {
		"git", "-C", b->repo_dir, "update-index", "--add", "--cacheinfo",
		mode ? mode : "100644", blob_sha, path, NULL
	};
	return run_cmd_argv(argv, NULL, 0, NULL, NULL, env) == 0 ? 0 : -1;
}

int
git_index_builder_write_tree(struct git_index_builder *b, char *out_tree_sha)
{
	char env_idx[2048];
	snprintf(env_idx, sizeof(env_idx), "GIT_INDEX_FILE=%s", b->index_file);
	const char *env[] = {env_idx, NULL};
	const char *argv[] = {"git", "-C", b->repo_dir, "write-tree", NULL};

	struct strbuf out;
	strbuf_init(&out, 128);
	if (run_cmd_argv(argv, NULL, 0, &out, NULL, env) != 0) {
		strbuf_free(&out);
		return -1;
	}

	while (out.len > 0 && (out.buf[out.len - 1] == '\n' || out.buf[out.len - 1] == '\r' || out.buf[out.len - 1] == ' '))
		out.buf[--out.len] = '\0';

	if (out.len == 40 || out.len == 64) {
		snprintf(out_tree_sha, 65, "%s", out.buf);
		strbuf_free(&out);
		return 0;
	}

	strbuf_free(&out);
	return -1;
}

void
git_index_builder_free(struct git_index_builder *b)
{
	if (b->index_file[0]) {
		unlink(b->index_file);
		b->index_file[0] = '\0';
	}
}

int
git_read_tree(const char *repo_dir, const char *ref, struct git_tree *out_tree)
{
	git_tree_init(out_tree);
	const char *dir = (repo_dir && *repo_dir) ? repo_dir : ".";
	const char *target_ref = (ref && *ref) ? ref : "HEAD";
	const char *argv[] = {"git", "-C", dir, "ls-tree", "-r", "--full-tree", target_ref, NULL};

	struct strbuf out;
	strbuf_init(&out, 16384);
	if (run_cmd_argv(argv, NULL, 0, &out, NULL, NULL) != 0) {
		strbuf_free(&out);
		return -1;
	}

	char *p = out.buf;
	while (*p) {
		char *next_line = strchr(p, '\n');
		if (next_line) *next_line = '\0';

		char *tab = strchr(p, '\t');
		if (tab) {
			*tab = '\0';
			const char *path = tab + 1;
			char mode[16] = {0}, type[16] = {0}, sha[65] = {0};
			if (sscanf(p, "%15s %15s %64s", mode, type, sha) == 3) {
				if (out_tree->count >= out_tree->cap) {
					size_t ncap = out_tree->cap == 0 ? 32 : out_tree->cap * 2;
					struct git_tree_entry *nentries = realloc(out_tree->entries, ncap * sizeof(struct git_tree_entry));
					if (!nentries) break;
					out_tree->entries = nentries;
					out_tree->cap = ncap;
				}
				struct git_tree_entry *e = &out_tree->entries[out_tree->count++];
				snprintf(e->mode, sizeof(e->mode), "%s", mode);
				snprintf(e->type, sizeof(e->type), "%s", type);
				snprintf(e->sha, sizeof(e->sha), "%s", sha);
				snprintf(e->path, sizeof(e->path), "%s", path);
			}
		}
		if (!next_line) break;
		p = next_line + 1;
	}

	strbuf_free(&out);
	return 0;
}

int
git_get_ref_commit(const char *repo_dir, const char *ref_name, char *out_commit_sha)
{
	const char *dir = (repo_dir && *repo_dir) ? repo_dir : ".";
	const char *argv[] = {"git", "-C", dir, "rev-parse", "--verify", "--quiet", ref_name, NULL};

	struct strbuf out, err;
	strbuf_init(&out, 128);
	strbuf_init(&err, 128);
	if (run_cmd_argv(argv, NULL, 0, &out, &err, NULL) != 0) {
		strbuf_free(&out);
		strbuf_free(&err);
		return -1;
	}
	strbuf_free(&err);

	while (out.len > 0 && (out.buf[out.len - 1] == '\n' || out.buf[out.len - 1] == '\r' || out.buf[out.len - 1] == ' '))
		out.buf[--out.len] = '\0';

	if (out.len == 40 || out.len == 64) {
		snprintf(out_commit_sha, 65, "%s", out.buf);
		strbuf_free(&out);
		return 0;
	}

	strbuf_free(&out);
	return -1;
}

int
git_create_commit(const char *repo_dir, const char *tree_sha, const char *parent_sha,
                  const char *msg, char *out_commit_sha)
{
	const char *dir = (repo_dir && *repo_dir) ? repo_dir : ".";
	const char *commit_msg = (msg && *msg) ? msg : "webcrawl snapshot";
	const char *env[] = {
		"GIT_AUTHOR_NAME=gitcrawl",
		"GIT_AUTHOR_EMAIL=gitcrawl@localhost",
		"GIT_COMMITTER_NAME=gitcrawl",
		"GIT_COMMITTER_EMAIL=gitcrawl@localhost",
		NULL
	};

	const char *argv[10];
	int argc = 0;
	argv[argc++] = "git";
	argv[argc++] = "-C";
	argv[argc++] = dir;
	argv[argc++] = "commit-tree";
	argv[argc++] = tree_sha;
	if (parent_sha && *parent_sha) {
		argv[argc++] = "-p";
		argv[argc++] = parent_sha;
	}
	argv[argc++] = "-m";
	argv[argc++] = commit_msg;
	argv[argc] = NULL;

	struct strbuf out;
	strbuf_init(&out, 128);

	int res = run_cmd_argv(argv, NULL, 0, &out, NULL, env);
	if (res != 0) {
		strbuf_free(&out);
		return -1;
	}

	while (out.len > 0 && (out.buf[out.len - 1] == '\n' || out.buf[out.len - 1] == '\r' || out.buf[out.len - 1] == ' '))
		out.buf[--out.len] = '\0';

	if (out.len == 40 || out.len == 64) {
		snprintf(out_commit_sha, 65, "%s", out.buf);
		strbuf_free(&out);
		return 0;
	}

	strbuf_free(&out);
	return -1;
}

int
git_update_ref(const char *repo_dir, const char *ref_name, const char *commit_sha, const char *old_commit_sha)
{
	const char *dir = (repo_dir && *repo_dir) ? repo_dir : ".";
	const char *argv[8];
	int argc = 0;
	argv[argc++] = "git";
	argv[argc++] = "-C";
	argv[argc++] = dir;
	argv[argc++] = "update-ref";
	argv[argc++] = ref_name;
	argv[argc++] = commit_sha;
	if (old_commit_sha && *old_commit_sha) {
		argv[argc++] = old_commit_sha;
	}
	argv[argc] = NULL;

	return run_cmd_argv(argv, NULL, 0, NULL, NULL, NULL);
}

int
git_show_diff(const char *repo_dir, const char *ref1, const char *ref2, const char *path)
{
	const char *dir = (repo_dir && *repo_dir) ? repo_dir : ".";
	const char *argv[10];
	int argc = 0;
	argv[argc++] = "git";
	argv[argc++] = "-C";
	argv[argc++] = dir;
	argv[argc++] = "diff";
	argv[argc++] = "--color=auto";

	char ref_parent[128];
	if (ref1 && ref2) {
		argv[argc++] = ref1;
		argv[argc++] = ref2;
	} else if (ref1) {
		snprintf(ref_parent, sizeof(ref_parent), "%s~1", ref1);
		argv[argc++] = ref_parent;
		argv[argc++] = ref1;
	}

	if (path && *path) {
		argv[argc++] = "--";
		argv[argc++] = path;
	}
	argv[argc] = NULL;

	return run_cmd_argv(argv, NULL, 0, NULL, NULL, NULL);
}

int
git_show_log(const char *repo_dir, const char *ref, const char *path, int limit)
{
	const char *dir = (repo_dir && *repo_dir) ? repo_dir : ".";
	char limit_str[32];
	const char *argv[12];
	int argc = 0;
	argv[argc++] = "git";
	argv[argc++] = "-C";
	argv[argc++] = dir;
	argv[argc++] = "log";
	argv[argc++] = "--stat";
	argv[argc++] = "--color=auto";

	if (limit > 0) {
		argv[argc++] = "-n";
		snprintf(limit_str, sizeof(limit_str), "%d", limit);
		argv[argc++] = limit_str;
	}
	argv[argc++] = (ref && *ref) ? ref : "HEAD";

	if (path && *path) {
		argv[argc++] = "--";
		argv[argc++] = path;
	}
	argv[argc] = NULL;

	return run_cmd_argv(argv, NULL, 0, NULL, NULL, NULL);
}

char *
git_read_file_at_ref(const char *repo_dir, const char *ref, const char *path, size_t *out_len)
{
	const char *dir = (repo_dir && *repo_dir) ? repo_dir : ".";
	char spec[2048];
	snprintf(spec, sizeof(spec), "%s:%s", (ref && *ref) ? ref : "HEAD", path);

	const char *argv[] = {"git", "-C", dir, "show", spec, NULL};
	struct strbuf out;
	strbuf_init(&out, 4096);

	if (run_cmd_argv(argv, NULL, 0, &out, NULL, NULL) != 0) {
		strbuf_free(&out);
		return NULL;
	}
	return strbuf_detach(&out, out_len);
}

int
git_run_gc(const char *repo_dir)
{
	const char *dir = (repo_dir && *repo_dir) ? repo_dir : ".";
	const char *argv[] = {"git", "-C", dir, "gc", "--prune=now", "--quiet", NULL};
	return run_cmd_argv(argv, NULL, 0, NULL, NULL, NULL) == 0 ? 0 : -1;
}
