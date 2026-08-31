#!/bin/sh
set -e

VERSION="1.0.0"
DIST_DIR="/tmp/gitcrawl-release-${VERSION}"

rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR/linux" "$DIST_DIR/windows" "$DIST_DIR/dist"

echo "Building Linux x86_64 release binary..."
gcc -std=c99 -Wall -Wextra -pedantic -O2 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -D_GNU_SOURCE     -DVERSION=\"${VERSION}\" strbuf.c entity.c sharder.c sanitizer.c parser.c git_plumbing.c approx_search.c fetcher.c gitcrawl.c     -lz -o "$DIST_DIR/linux/gitcrawl"
cp LICENSE README.md gitcrawl.1 "$DIST_DIR/linux/"
tar -czf "$DIST_DIR/dist/gitcrawl-linux-amd64.tar.gz" -C "$DIST_DIR/linux" .

echo "Building Windows x86_64 release binary..."
x86_64-w64-mingw32-gcc -std=c99 -Wall -Wextra -O2 -DVERSION=\"${VERSION}\"     strbuf.c entity.c sharder.c sanitizer.c parser.c git_plumbing.c approx_search.c fetcher.c gitcrawl.c     -o "$DIST_DIR/windows/gitcrawl.exe"
cp LICENSE README.md "$DIST_DIR/windows/"
(cd "$DIST_DIR/windows" && zip -q "$DIST_DIR/dist/gitcrawl-windows-amd64.zip" gitcrawl.exe LICENSE README.md)

echo "Building source distribution..."
make dist
cp "gitcrawl-${VERSION}.tar.gz" "$DIST_DIR/dist/"

echo "Generating SHA256SUMS.txt..."
(cd "$DIST_DIR/dist" && sha256sum * > SHA256SUMS.txt)

echo "Release assets generated at $DIST_DIR/dist:"
ls -lh "$DIST_DIR/dist"
cat "$DIST_DIR/dist/SHA256SUMS.txt"
