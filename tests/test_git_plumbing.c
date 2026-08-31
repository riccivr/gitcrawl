#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../git_plumbing.h"

int main(void) {
	printf("Running test_git_plumbing...\n");

	const char *test_repo = "/tmp/gitcrawl_test_repo";
	int sres = system("rm -rf /tmp/gitcrawl_test_repo");
	(void)sres;

	int res = git_repo_init(test_repo);
	assert(res == 0);

	char blob_sha[64] = {0};
	const char *test_data = "# Test Document\n\nHello Gitcrawl\n";
	res = git_write_blob(test_repo, test_data, strlen(test_data), blob_sha);
	assert(res == 0);
	assert(strlen(blob_sha) == 40);

	struct git_index_builder b;
	git_index_builder_init(&b, test_repo, NULL);
	res = git_index_builder_add_blob(&b, "100644", blob_sha, "archive/example.com/index.md");
	assert(res == 0);

	char tree_sha[64] = {0};
	res = git_index_builder_write_tree(&b, tree_sha);
	assert(res == 0);
	assert(strlen(tree_sha) == 40);
	git_index_builder_free(&b);

	char commit_sha[64] = {0};
	res = git_create_commit(test_repo, tree_sha, NULL, "test commit", commit_sha);
	assert(res == 0);
	assert(strlen(commit_sha) == 40);

	res = git_update_ref(test_repo, "refs/heads/archive", commit_sha);
	assert(res == 0);

	size_t read_len = 0;
	char *read_back = git_read_file_at_ref(test_repo, "refs/heads/archive", "archive/example.com/index.md", &read_len);
	assert(read_back != NULL);
	assert(strcmp(read_back, test_data) == 0);

	free(read_back);
	sres = system("rm -rf /tmp/gitcrawl_test_repo");
	(void)sres;

	printf("test_git_plumbing passed!\n");
	return 0;
}
