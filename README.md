gitcrawl
========
Lightweight web snapshot utility and crawler for Git repositories in clean C99.

`gitcrawl` snapshots web pages and commits their history directly into Git repositories, storing diff-friendly Markdown alongside original compressed HTML and JSON metadata.

Based on the core concept in [*Preserving the Web with Git*](https://riccivr.github.io/blog/preserving-the-web-with-git.html).

[![gitcrawl Demo](assets/demo.gif)](https://asciinema.org/a/ZxDdqnFFIVMekWNU)

Overview
--------
`gitcrawl` archives web pages into a Git branch (`archive` by default) with a clean three-file structure per URL:

* `index.md`: Sanitized HTML converted to Markdown, designed for readable `git diff` and `git log -S` exploration.
* `index.html.gz`: Deflate-compressed raw HTML body for faithful original backup.
* `metadata.json`: HTTP status code, response headers, timestamps, and payload sizes with strict JSON escaping.

Features
--------
* **In-Memory Git Plumbing**: Streams blobs directly into `git hash-object -w --stdin` and builds Git trees in memory without writing temporary files to disk.
* **Safe Process Execution**: Invokes `git` and `curl` directly via `execvp` argument vectors with piped I/O, completely avoiding `/bin/sh` shell interpolation to prevent command injection.
* **SHA-1 & SHA-256 Support**: Fully compatible with both standard SHA-1 and modern SHA-256 Git repositories.
* **DOM Noise Sanitization**: Strips scripts, stylesheets, tracking pixels (`width=1`, `height=1`, `display:none`), cookie banners, and volatile tokens (CSRF nonces, session IDs) before Markdown generation.
* **Two-Tier Storage**: Produces human-friendly Markdown for version tracking while retaining full original HTML payloads in compressed format.
* **Fuzzy History Search**: Integrates the official embedded single-header [`approx.h`](https://github.com/riccivr/approx) library (v1.2.0) with Damerau-Levenshtein distance and a bounded Min-Heap for fast top-N search across archived paths.
* **Minimal Dependencies**: Written in POSIX C99 requiring only `libc`, `zlib`, with `git` and `curl` available on `$PATH`.
* **Conditional GET**: Sends `If-None-Match` from stored `metadata.json` and skips a new commit on HTTP 304.
* **robots.txt**: `crawl` loads `/robots.txt` for the start host (`User-agent: *` and `gitcrawl`) and honors `Disallow` plus `Crawl-delay`.
* **Same-host assets**: Captures `<img src>` and `<link href>` files next to the page under `assets/`.

Architecture
------------

```
Raw Web Page / stdin
       │
       ▼
[ Fetch Engine ] ──> Direct execvp invocation of curl / stdin stream
       │
       ├── Original Body ────────> [ Gzip Compression ] ──> index.html.gz
       │
       ▼
[ DOM Sanitizer ] ──> Strip scripts, tracking pixels, ads, CSRF nonces
       │
       ▼
[ Stream Parser ] ──> Convert clean DOM into normalized Markdown ──> index.md
       │
       ▼
[ Git Plumbing  ] ──> Stream blobs directly to hash-object & commit to Git ref
```

Installation
------------

### Build from source (recommended)

Requirements: a C99 compiler (`gcc` or `clang`), `make`, `zlib`, with `git` and `curl` on `$PATH`.

```sh
git clone https://github.com/riccivr/gitcrawl.git
cd gitcrawl
make
sudo make install
```

To install into another prefix (e.g. `~/.local`):

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
sudo dpkg -i gitcrawl_1.1.0_amd64.deb
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
Static binaries for Linux, macOS, and Windows are available from [GitHub Releases](https://github.com/riccivr/gitcrawl/releases).

Running tests & benchmarks
---------------------------
`gitcrawl` includes a comprehensive multi-tier test suite covering unit tests, POSIX edge cases, property-based invariants, fuzz resilience, high-throughput stress tests, sanitizers, and benchmarks:

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
| `-w delay_ms` | Delay in milliseconds between crawl requests (default: `0`) |
| `-s` | Restrict crawler to the same domain (default: off) |
| `-f format` | Output format for `show` (`md`, `html`, `json`) |
| `-i` | Read content from standard input for the specified URL |
| `-z` | Enable fuzzy search scoring via `approx.h` |
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
  "etag": "\"1f8a-5e290\"",
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
Fuzzy-rank all archived paths and historical commits using `approx.h` scoring:

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
Starting crawl: https://docs.kernel.org/process/ (depth=2, max_pages=20, same_domain=true)
[1/20] Crawling (d=0): https://docs.kernel.org/process/
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

Performance measured on Linux x86_64 using standard C99 builds compiled with `-O2`:

### Micro-Benchmarks (`make bench`)

| Subsystem / Operation | Iterations | Latency / Speed | Throughput |
|---|---|---|---|
| **DOM Sanitizer & Noise Stripper** | 50,000 | ~0.95 µs / op (~1,000,000 ops/sec) | **~400 MB/s** |
| **HTML-to-Markdown Parser Engine** | 50,000 | ~2.00 µs / op (~500,000 ops/sec) | **~200 MB/s** |
| **Fuzzy Path Ranking (`approx.h`)** | 200,000 | ~4.50 µs / query | **~220,000 QPS** |

### Macro Ingestion Pipeline & Storage Efficiency

| Metric / Pipeline Stage | Performance |
|---|---|
| **In-Memory Pipe Ingestion** | **100+ pages/sec** directly to `.git/objects` via stdin plumbing |
| **Gzip Payload Compression** | **~75–85% raw size reduction** |
| **Git Tree Delta Compression** | **~90–95% storage efficiency** across successive revisions |
| **Repository Optimization (`gitcrawl gc`)** | **Sub-second packfile compaction** |
