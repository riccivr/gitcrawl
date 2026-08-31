/* See LICENSE file for copyright and license details. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "git_plumbing.h"
#include "strbuf.h"

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

	char cmd[4096];
	snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\" && git -C \"%s\" init --quiet", dir, dir);
	return system(cmd) == 0 ? 0 : -1;
}

int
git_write_blob(const char *repo_dir, const void *data, size_t len, char *out_sha)
{
	static int seq = 0;
	char tmp_path[512];
#if defined(_WIN32)
	const char *tmp_dir = getenv("TEMP");
	if (!tmp_dir) tmp_dir = getenv("TMP");
	if (!tmp_dir) tmp_dir = ".";
	snprintf(tmp_path, sizeof(tmp_path), "%s/gitcrawl_blob_%d_%d.tmp", tmp_dir, (int)getpid(), seq++);
#else
	snprintf(tmp_path, sizeof(tmp_path), "/tmp/gitcrawl_blob_%d_%d.tmp", (int)getpid(), seq++);
#endif

	FILE *f = fopen(tmp_path, "wb");
	if (!f) return -1;
	if (len > 0 && fwrite(data, 1, len, f) != len) {
		fclose(f);
		unlink(tmp_path);
		return -1;
	}
	fclose(f);

	char cmd[1024];
	snprintf(cmd, sizeof(cmd), "git -C \"%s\" hash-object -w \"%s\" 2>/dev/null",
	         (repo_dir && *repo_dir) ? repo_dir : ".", tmp_path);

	FILE *fp = popen(cmd, "r");
	if (!fp) {
		unlink(tmp_path);
		return -1;
	}

	char buf[128] = {0};
	char *res = fgets(buf, sizeof(buf), fp);
	int status = pclose(fp);
	unlink(tmp_path);

	if (status == 0 && res) {
		size_t rlen = strlen(buf);
		while (rlen > 0 && (buf[rlen-1] == '\n' || buf[rlen-1] == '\r'))
			buf[--rlen] = '\0';
		if (rlen == 40) {
			snprintf(out_sha, 64, "%s", buf);
			return 0;
		}
	}
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
		char commit[64] = {0};
		if (git_get_ref_commit(dir, base_ref, commit) == 0) {
			char cmd[4096];
			snprintf(cmd, sizeof(cmd), "GIT_INDEX_FILE=\"%s\" git -C \"%s\" read-tree \"%s\" 2>/dev/null",
			         b->index_file, dir, commit);
			int res = system(cmd);
			(void)res;
		}
	}
	return 0;
}

int
git_index_builder_add_blob(struct git_index_builder *b, const char *mode, const char *blob_sha, const char *path)
{
	char cmd[4096];
	snprintf(cmd, sizeof(cmd),
	         "GIT_INDEX_FILE=\"%s\" git -C \"%s\" update-index --add --cacheinfo %s %s \"%s\"",
	         b->index_file, b->repo_dir, mode ? mode : "100644", blob_sha, path);
	return system(cmd) == 0 ? 0 : -1;
}

int
git_index_builder_write_tree(struct git_index_builder *b, char *out_tree_sha)
{
	char cmd[4096];
	snprintf(cmd, sizeof(cmd), "GIT_INDEX_FILE=\"%s\" git -C \"%s\" write-tree 2>/dev/null",
	         b->index_file, b->repo_dir);

	FILE *fp = popen(cmd, "r");
	if (!fp) return -1;

	char buf[128] = {0};
	char *res = fgets(buf, sizeof(buf), fp);
	int status = pclose(fp);

	if (status == 0 && res) {
		size_t len = strlen(buf);
		while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
			buf[--len] = '\0';
		if (strlen(buf) == 40) {
			snprintf(out_tree_sha, 64, "%s", buf);
			return 0;
		}
	}
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

	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "git -C \"%s\" ls-tree -r --full-tree \"%s\" 2>/dev/null",
	         repo_dir ? repo_dir : ".", ref);

	FILE *fp = popen(cmd, "r");
	if (!fp)
		return -1;

	char line[2048];
	while (fgets(line, sizeof(line), fp)) {
		char mode[16] = {0};
		char type[16] = {0};
		char sha[64] = {0};
		char path[1024] = {0};

		char *tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = '\0';
		snprintf(path, sizeof(path), "%s", tab + 1);
		size_t plen = strlen(path);
		while (plen > 0 && (path[plen-1] == '\n' || path[plen-1] == '\r'))
			path[--plen] = '\0';

		if (sscanf(line, "%15s %15s %63s", mode, type, sha) == 3) {
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
	pclose(fp);
	return 0;
}

int
git_get_ref_commit(const char *repo_dir, const char *ref_name, char *out_commit_sha)
{
	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "git -C \"%s\" rev-parse --verify \"%s\" 2>/dev/null",
	         repo_dir ? repo_dir : ".", ref_name);

	FILE *fp = popen(cmd, "r");
	if (!fp)
		return -1;

	char buf[128] = {0};
	char *res = fgets(buf, sizeof(buf), fp);
	int status = pclose(fp);

	if (status == 0 && res) {
		size_t len = strlen(buf);
		while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
			buf[--len] = '\0';
		if (strlen(buf) == 40) {
			snprintf(out_commit_sha, 64, "%s", buf);
			return 0;
		}
	}
	return -1;
}

int
git_create_commit(const char *repo_dir, const char *tree_sha, const char *parent_sha,
                 const char *msg, char *out_commit_sha)
{
	char cmd[4096];
	if (parent_sha && *parent_sha) {
		snprintf(cmd, sizeof(cmd), "git -C \"%s\" commit-tree \"%s\" -p \"%s\" -m \"%s\" 2>/dev/null",
		         repo_dir ? repo_dir : ".", tree_sha, parent_sha, msg ? msg : "webcrawl snapshot");
	} else {
		snprintf(cmd, sizeof(cmd), "git -C \"%s\" commit-tree \"%s\" -m \"%s\" 2>/dev/null",
		         repo_dir ? repo_dir : ".", tree_sha, msg ? msg : "initial webcrawl snapshot");
	}

	FILE *fp = popen(cmd, "r");
	if (!fp)
		return -1;

	char buf[128] = {0};
	char *res = fgets(buf, sizeof(buf), fp);
	int status = pclose(fp);

	if (status == 0 && res) {
		size_t len = strlen(buf);
		while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
			buf[--len] = '\0';
		if (strlen(buf) == 40) {
			snprintf(out_commit_sha, 64, "%s", buf);
			return 0;
		}
	}
	return -1;
}

int
git_update_ref(const char *repo_dir, const char *ref_name, const char *commit_sha)
{
	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "git -C \"%s\" update-ref \"%s\" \"%s\"",
	         repo_dir ? repo_dir : ".", ref_name, commit_sha);
	return system(cmd) == 0 ? 0 : -1;
}

int
git_show_diff(const char *repo_dir, const char *ref1, const char *ref2, const char *path)
{
	char cmd[4096];
	if (ref1 && ref2) {
		if (path && *path) {
			snprintf(cmd, sizeof(cmd), "git -C \"%s\" diff --color=auto \"%s\" \"%s\" -- \"%s\"",
			         repo_dir ? repo_dir : ".", ref1, ref2, path);
		} else {
			snprintf(cmd, sizeof(cmd), "git -C \"%s\" diff --color=auto \"%s\" \"%s\"",
			         repo_dir ? repo_dir : ".", ref1, ref2);
		}
	} else if (ref1) {
		if (path && *path) {
			snprintf(cmd, sizeof(cmd), "git -C \"%s\" diff --color=auto \"%s\"~1 \"%s\" -- \"%s\"",
			         repo_dir ? repo_dir : ".", ref1, ref1, path);
		} else {
			snprintf(cmd, sizeof(cmd), "git -C \"%s\" diff --color=auto \"%s\"~1 \"%s\"",
			         repo_dir ? repo_dir : ".", ref1, ref1);
		}
	}
	return system(cmd);
}

int
git_show_log(const char *repo_dir, const char *ref, const char *path, int limit)
{
	char cmd[4096];
	if (limit > 0) {
		snprintf(cmd, sizeof(cmd), "git -C \"%s\" log -n %d --stat --color=auto \"%s\" -- \"%s\"",
		         repo_dir ? repo_dir : ".", limit, ref ? ref : "HEAD", path ? path : "");
	} else {
		snprintf(cmd, sizeof(cmd), "git -C \"%s\" log --stat --color=auto \"%s\" -- \"%s\"",
		         repo_dir ? repo_dir : ".", ref ? ref : "HEAD", path ? path : "");
	}
	return system(cmd);
}

char *
git_read_file_at_ref(const char *repo_dir, const char *ref, const char *path, size_t *out_len)
{
	char cmd[4096];
	snprintf(cmd, sizeof(cmd), "git -C \"%s\" show \"%s:%s\" 2>/dev/null",
	         repo_dir ? repo_dir : ".", ref ? ref : "HEAD", path);

	FILE *fp = popen(cmd, "r");
	if (!fp) return NULL;

	struct strbuf sb;
	strbuf_init(&sb, 4096);

	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		strbuf_append_len(&sb, buf, n);
	}
	int status = pclose(fp);
	if (status != 0) {
		strbuf_free(&sb);
		return NULL;
	}
	return strbuf_detach(&sb, out_len);
}

int
git_run_gc(const char *repo_dir)
{
	char cmd[2048];
	snprintf(cmd, sizeof(cmd), "git -C \"%s\" gc --prune=now --quiet", repo_dir ? repo_dir : ".");
	return system(cmd) == 0 ? 0 : -1;
}
