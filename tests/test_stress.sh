#!/bin/sh
set -e
echo "Running Stress & High-Throughput Ingestion Tests..."

TEST_REPO="/tmp/gitcrawl_stress_repo"
rm -rf "$TEST_REPO"
mkdir -p "$TEST_REPO"

BIG_HTML="/tmp/gitcrawl_big_doc.html"
python3 -c '
with open("'"$BIG_HTML"'", "w") as f:
    f.write("<!DOCTYPE html><html><head><title>Large Document</title></head><body>\n")
    f.write("<h1>Massive Archive Stress Test</h1>\n")
    for i in range(2500):
        f.write(f"<h2>Section {i}</h2><p>Paragraph with dynamic data {i} and <strong>bold text</strong> and <a href=\"https://example.com/page/{i}\">link {i}</a>.</p>\n")
        f.write("<ul><li>Alpha</li><li>Beta</li><li>Gamma</li></ul>\n")
    f.write("</body></html>\n")
'

echo "  [stress] Ingesting 2.5MB large HTML document..."
cat "$BIG_HTML" | ./gitcrawl archive -d "$TEST_REPO" -i https://example.com/massive-document
rm -f "$BIG_HTML"

echo "  [stress] Ingesting 50 sequential version snapshots..."
for i in $(seq 1 50); do
    echo "<h1>Doc Version $i</h1><p>Snapshot increment $i</p>" | ./gitcrawl archive -d "$TEST_REPO" -i https://example.com/rapid-snapshots -m "snapshot $i" >/dev/null
done

COUNT=$(git -C "$TEST_REPO" rev-list --count refs/heads/archive)
[ "$COUNT" -ge 51 ]
echo "  [stress] Successfully ingested $COUNT commits into content-addressable store."

SEARCH_RES=$(./gitcrawl search -d "$TEST_REPO" -z "rapid-snapshots")
echo "$SEARCH_RES" | grep -q "example.com/rapid-snapshots"
echo "  [stress] Fuzzy index search verified under high commit volume."

./gitcrawl gc -d "$TEST_REPO"
echo "  [stress] Repository compaction & GC verified."

rm -rf "$TEST_REPO"
echo "Stress & High-Throughput Ingestion Tests passed successfully!"
