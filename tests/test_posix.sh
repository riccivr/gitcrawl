#!/bin/sh
set -e
echo "Running POSIX CLI Compliance & Edge Case Tests..."

TEST_REPO="/tmp/gitcrawl_posix_repo"
rm -rf "$TEST_REPO"

# Test 1: Version flag -v outputs gitcrawl-VERSION and exits 0
VERSION_OUT=$(./gitcrawl -v)
echo "$VERSION_OUT" | grep -q "^gitcrawl-[0-9]"
echo "  [posix] -v flag verified ($VERSION_OUT)"

# Test 2: Help flag -h outputs usage and exits 0
HELP_OUT=$(./gitcrawl -h)
echo "$HELP_OUT" | grep -q "usage: gitcrawl"
echo "  [posix] -h flag verified"

# Test 3: Invalid flag outputs usage and exits with code 1
set +e
./gitcrawl -invalid_xyz >/dev/null 2>&1
INVALID_STATUS=$?
set -e
[ "$INVALID_STATUS" -eq 1 ]
echo "  [posix] invalid flag exit code verified (1)"

# Test 4: Missing arguments exit code verified
set +e
./gitcrawl archive >/dev/null 2>&1
MISSING_ARG_STATUS=$?
set -e
[ "$MISSING_ARG_STATUS" -eq 1 ]
echo "  [posix] missing arguments exit code verified (1)"

# Test 5: Pipe ingestion into custom repo and custom branch
echo "<h1>POSIX Test</h1><p>Stdin streaming validation</p>" | ./gitcrawl archive -d "$TEST_REPO" -b feature-posix -i https://example.com/posix-spec
COMMIT_SHA=$(git -C "$TEST_REPO" rev-parse refs/heads/feature-posix)
[ -n "$COMMIT_SHA" ]
echo "  [posix] custom branch & repo ingestion verified ($COMMIT_SHA)"

# Test 6: Custom commit message flag -m
echo "<h1>POSIX Update</h1><p>Second revision</p>" | ./gitcrawl archive -d "$TEST_REPO" -b feature-posix -m "feat: custom posix commit message" -i https://example.com/posix-spec
LOG_OUT=$(./gitcrawl log -d "$TEST_REPO" -b feature-posix https://example.com/posix-spec)
echo "$LOG_OUT" | grep -q "feat: custom posix commit message"
echo "  [posix] -m custom message flag verified"

# Test 7: Output formats: md, json
MD_OUT=$(./gitcrawl show -d "$TEST_REPO" -b feature-posix -f md https://example.com/posix-spec)
echo "$MD_OUT" | grep -q "# POSIX Update"

JSON_OUT=$(./gitcrawl show -d "$TEST_REPO" -b feature-posix -f json https://example.com/posix-spec)
echo "$JSON_OUT" | grep -q '"url": "https://example.com/posix-spec"'
echo "  [posix] output formats (md, json) verified"

# Test 8: List archived URLs
LIST_OUT=$(./gitcrawl list -d "$TEST_REPO" -b feature-posix)
echo "$LIST_OUT" | grep -q "archive/example.com/posix-spec/index.md"
echo "  [posix] list command verified"

# Test 9: Git GC command
./gitcrawl gc -d "$TEST_REPO"
echo "  [posix] gc repository maintenance verified"

rm -rf "$TEST_REPO"
echo "POSIX CLI Compliance Tests passed successfully!"
