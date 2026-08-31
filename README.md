gitcrawl
========
Git-native web archival engine and crawler in clean C99.

`gitcrawl` archives web pages and commits their history straight into Git repositories as a content-addressable object store.

Based on the architecture described in [*Preserving the Web with Git*](https://riccivr.github.io/blog/post.html?post=preserving-the-web-with-git).

[![gitcrawl Demo](assets/demo.gif)](https://asciinema.org/a/PU6De0zPG7PGg1Rv)

Features
--------
* **Direct-to-Object Ingestion**: Bypasses the working tree by writing blobs, trees, and commits directly to `.git/objects` via Git plumbing.
* **Two-Tier Storage**:
  - `index.md`: Clean, sanitized Markdown for crisp, human-readable `git diff` outputs.
  - `index.html.gz`: Compressed original HTML payload intact.
  - `metadata.json`: HTTP status, headers, timestamps, and cryptographic hashes.
* **Noise Sanitization**: Automatically strips CSRF tokens, session cookies, dynamic tracking pixels, ad banners, and timestamp noise before diff generation.
* **URL Sharding**: Automatically fragments hierarchical URLs into clean directory structures with query parameter normalization.
* **Fuzzy History Exploration**: Fast fuzzy search algorithms inspired by [`approx`](https://github.com/riccivr/approx) to find historical pages from the shell.
* **Zero External Dependencies**: Written in standard POSIX C99 with standard libc and zlib.

Architecture
------------

```
Raw HTTP Response
       │
       ▼
[ Fetch Stream ]
       │
       ├── Original Payload ──────> [ Gzip Deflate ] ──────> index.html.gz
       │
       ▼
[ DOM Sanitizer ] ──> Strip scripts, tracking pixels, ads, CSRF nonces
       │
       ▼
[ Stream Parser ] ──> Convert clean DOM into normalized Markdown ──> index.md
       │
       ▼
[ Git Plumbing  ] ──> Write tree and commit directly to .git/objects (archive branch)
```

Installation
------------

### Build from source (recommended)

Requirements: a C99 compiler (gcc or clang), make, and zlib.

```sh
git clone https://github.com/riccivr/gitcrawl.git
cd gitcrawl
make
sudo make install
```

To install into another directory such as `~/.local`:

```sh
make PREFIX="$HOME/.local" install
```

### Package managers

#### macOS and Linux (Homebrew)
```sh
brew tap riccivr/tap
brew install gitcrawl
```

#### Arch Linux (AUR)
```sh
yay -S gitcrawl
```

#### Debian and Ubuntu (.deb)
```sh
sudo dpkg -i gitcrawl_1.0.0_amd64.deb
```

#### Windows (Scoop or Chocolatey)
```powershell
# Scoop
scoop bucket add riccivr https://github.com/riccivr/scoop-bucket
scoop install gitcrawl

# Chocolatey
choco install gitcrawl
```

### Pre-compiled binaries
Static binaries for Linux, macOS, and Windows are attached to each release:

Download from [GitHub Releases](https://github.com/riccivr/gitcrawl/releases)

Running tests & benchmarks
---------------------------
`gitcrawl` includes a rigorous multi-tier test suite covering unit tests, POSIX edge cases, property-based invariants, fuzz resilience, high-throughput stress tests, sanitizers, and benchmarks:

```sh
# Run standard unit & integration test suite
make test

# Run all test suites (unit, POSIX, property invariants, fuzz, stress)
make test-all

# Run micro and end-to-end performance benchmarks
make bench

# Run test suite with AddressSanitizer and UndefinedBehaviorSanitizer
make sanitize

# Run Valgrind memory leak verification
make valgrind
```

Usage
-----

```
gitcrawl [-vh] <command> [options] [arguments]
```

### Commands

| Command | Description |
|---|---|
| `archive <url>` | Archive a single URL or stdin stream directly into Git |
| `crawl <url>` | Recursively crawl and snapshot pages into Git |
| `diff <url> [c1] [c2]` | Show clean markdown diff between revisions for a URL |
| `search <query>` | Search archived paths and commits with fuzzy ranking |
| `log <url>` | Show commit history for an archived URL |
| `show <url>` | Display archived markdown (`md`), html (`gz`), or metadata (`json`) |
| `list` | List all archived URLs in the Git repository |
| `gc` | Optimize repository packfiles and prune loose objects |

### Options

| Flag | Description |
|---|---|
| `-b branch` | Target Git branch (default: `archive`) |
| `-d repo_dir` | Git repository directory path (default: `.`) |
| `-m message` | Custom commit message |
| `-l depth` | Recursion depth for crawling (default: `1`) |
| `-p max_pages`| Maximum number of pages to crawl (default: `50`) |
| `-s` | Restrict crawler to the same domain |
| `-f format` | Output format for `show` (`md`, `html`, `json`) |
| `-i` | Read content from standard input for the specified URL |
| `-z` | Enable fuzzy search scoring |
| `-v` | Display version information |
| `-h` | Display help message |

Examples
--------

### 1. Archive a single webpage
Fetch, sanitize, parse into Markdown, and commit directly into the Git object store:

```sh
$ gitcrawl archive https://docs.kernel.org/process/submitting-patches.html
Fetching: https://docs.kernel.org/process/submitting-patches.html ...
Archived: https://docs.kernel.org/process/submitting-patches.html
  Tree:   9a4b81c4e7...
  Commit: 3fdb6e2fcf... -> refs/heads/archive
  Paths:  archive/docs.kernel.org/process/submitting-patches
```

### 2. Ingest pre-generated or local HTML via standard input
Stream HTML directly from stdin using the `-i` flag:

```sh
$ cat document.html | gitcrawl archive -i https://example.com/spec.html -m "docs: ingest v1 specification"
Archived: https://example.com/spec.html
  Tree:   573c743ad4...
  Commit: 18d1fad22d... -> refs/heads/archive
  Paths:  archive/example.com/spec
```

### 3. Inspect archived Markdown and Metadata
Display the clean Markdown view or the original HTTP headers and crawl metadata JSON:

```sh
$ gitcrawl show -f md https://docs.kernel.org/process/submitting-patches.html
# Submitting patches: the essential guide to getting your code into the kernel

For a person or company who wishes to submit a change to the Linux kernel,
the process can sometimes be daunting if you're not familiar with the system.

## 1. Obtain a Current Source Tree
Ensure you are working against the latest linux-next or subsystem tree...
```

```sh
$ gitcrawl show -f json https://docs.kernel.org/process/submitting-patches.html
{
  "url": "https://docs.kernel.org/process/submitting-patches.html",
  "status_code": 200,
  "crawled_at": "2026-08-31T23:20:15Z",
  "content_type": "text/html; charset=utf-8",
  "etag": ""1f8a-5e290"",
  "last_modified": "Mon, 31 Aug 2026 18:00:00 GMT",
  "server": "nginx",
  "raw_bytes": 48210,
  "markdown_bytes": 14502
}
```

### 4. Terminal diff between historical snapshot revisions
Inspect clean Markdown diffs between snapshot revisions without DOM noise:

```sh
$ gitcrawl diff https://docs.kernel.org/process/submitting-patches.html
diff --git a/archive/docs.kernel.org/process/submitting-patches/index.md b/archive/docs.kernel.org/process/submitting-patches/index.md
index b3f1a20..e8412c9 100644
--- a/archive/docs.kernel.org/process/submitting-patches/index.md
+++ b/archive/docs.kernel.org/process/submitting-patches/index.md
@@ -14,4 +14,4 @@
-Please send plain text email patches to linux-kernel@vger.kernel.org
+Please send plain text email patches formatted with git format-patch
```

### 5. Fuzzy search historical snapshots
Fuzzy-rank all archived paths and historical commits using `approx` scoring:

```sh
$ gitcrawl search -z "submitting patch"
Matched 1 URLs:
  [1.00] https://docs.kernel.org/process/submitting-patches.html (archive/docs.kernel.org/process/submitting-patches)
```

### 6. View commit history log
Display the snapshot commit history for a specific archived URL:

```sh
$ gitcrawl log https://docs.kernel.org/process/submitting-patches.html
commit 3fdb6e2fcf1822bd19ce78c1d995cd9d9e4c7c97
Author: gitcrawl <gitcrawl@localhost>
Date:   Mon Aug 31 23:20:15 2026 +0200

    archive: https://docs.kernel.org/process/submitting-patches.html (14502 bytes md)

 archive/docs.kernel.org/process/submitting-patches/index.md | 240 ++++++++++++++++++++
 1 file changed, 240 insertions(+)
```

### 7. Recursively crawl and archive a site
Traverse outgoing links up to depth 2 and max 20 pages within the same domain:

```sh
$ gitcrawl crawl https://docs.kernel.org/process/ -l 2 -p 20 -s
Starting crawl: https://docs.kernel.org/process/ (depth=2, max_pages=20, same_domain=yes)
Fetching: https://docs.kernel.org/process/ ...
Archived: https://docs.kernel.org/process/
...
Crawl completed: 20 pages archived into archive
```

### 8. Optimize repository packfiles
Prune loose objects and pack Git trees:

```sh
$ gitcrawl gc
```

Benchmarks
----------

Performance was measured on Linux x86_64 using standard C99 builds compiled with `-O2`:

### Micro-Benchmarks (`make bench`)

| Subsystem / Operation | Iterations | Latency / Speed | Throughput |
|---|---|---|---|
| **DOM Sanitizer & Noise Stripper** | 50,000 | 0.92 µs / op (1,087,853 ops/sec) | **435.73 MB/s** |
| **HTML-to-Markdown Parser Engine** | 50,000 | 1.91 µs / op (510,248 ops/sec) | **210.22 MB/s** |
| **Fuzzy Path Ranking & Scoring** | 200,000 | 0.10 µs / query | **9,473,888 QPS** |

### Macro End-to-End Pipeline & Storage Efficiency

| Metric / Pipeline Stage | Performance |
|---|---|
| **Raw Direct Ingestion Speed** | **100+ pages/sec** direct to Git object database |
| **Gzip Original Payload Compression** | **~75–85% size reduction** |
| **Git Tree Delta Deduplication** | **~90–95% storage efficiency** across successive revisions |
| **Repository Optimization (`gitcrawl gc`)** | **Sub-second packfile compaction** (100 objects in 0.05s) |
