#!/bin/sh
set -e
echo "===================================================="
echo "      gitcrawl End-to-End System Benchmark         "
echo "===================================================="
echo ""

# 1. Run C microbenchmarks
./tests/benchmark

# 2. Run E2E Ingestion Benchmark
echo "--- Benchmark 4: End-to-End Git Ingestion Pipeline ---"
BENCH_REPO="/tmp/gitcrawl_bench_repo"
rm -rf "$BENCH_REPO"
mkdir -p "$BENCH_REPO"

NUM_PAGES=100
echo "  Ingesting $NUM_PAGES synthetic web pages into direct Git object store..."

for i in $(seq 1 $NUM_PAGES); do
    echo "<h1>Page $i</h1><p>Content for endpoint $i with <a href='https://example.com/item/$i'>link</a>.</p>" | ./gitcrawl archive -d "$BENCH_REPO" -i "https://example.com/bench/page-$i" >/dev/null
done

COUNT=$(git -C "$BENCH_REPO" rev-list --count refs/heads/archive)
echo "  Archived commits count: $COUNT"

# 3. Benchmark Repo Compaction
echo ""
echo "--- Benchmark 5: Repository Packfile Optimization (gc) ---"
./gitcrawl gc -d "$BENCH_REPO"

rm -rf "$BENCH_REPO"
echo ""
echo "===================================================="
echo "End-to-End System Benchmark completed successfully!"
echo "===================================================="
