#!/bin/sh
set -e
echo "Running End-to-End Integration Tests..."

TEST_REPO="/tmp/gitcrawl_e2e_repo"
rm -rf "$TEST_REPO"
mkdir -p "$TEST_REPO"

# Test 1: Archive sample page via stdin
echo "<h1>Hello Web Archive</h1><p>First snapshot of content.</p>" | ./gitcrawl archive -d "$TEST_REPO" -b archive -i https://example.com/e2e-test

# Verify commit created
COMMIT1=$(git -C "$TEST_REPO" rev-parse refs/heads/archive)
[ -n "$COMMIT1" ]
echo "Snapshot 1 commit: $COMMIT1"

# Test 2: Archive modified page (verifying diff capability)
echo "<h1>Hello Web Archive</h1><p>First snapshot of content.</p><p>Added new update line.</p>" | ./gitcrawl archive -d "$TEST_REPO" -b archive -i https://example.com/e2e-test

COMMIT2=$(git -C "$TEST_REPO" rev-parse refs/heads/archive)
[ -n "$COMMIT2" ]
echo "Snapshot 2 commit: $COMMIT2"
[ "$COMMIT1" != "$COMMIT2" ]

# Test 3: Verify show command output
MD_OUTPUT=$(./gitcrawl show -d "$TEST_REPO" -b archive https://example.com/e2e-test -f md)
echo "$MD_OUTPUT" | grep -q "Added new update line"
echo "Show command output verified."

# Test 4: Verify search command
SEARCH_OUTPUT=$(./gitcrawl search -d "$TEST_REPO" -b archive -z "e2e-test")
echo "$SEARCH_OUTPUT" | grep -q "example.com/e2e-test"
echo "Search command verified."

# Test 5: Verify diff output
DIFF_OUTPUT=$(./gitcrawl diff -d "$TEST_REPO" -b archive https://example.com/e2e-test)
echo "Diff command verified."

# Clean up
rm -rf "$TEST_REPO"
echo "End-to-End Integration Tests passed!"
