gitcrawl
========
Git-native web archival engine and crawler in clean C99.

`gitcrawl` archives web pages and commits their history straight into Git repositories as a content-addressable object store.

Based on the architecture described in [*Preserving the Web with Git*](https://riccivr.github.io/blog/post.html?post=preserving-the-web-with-git).

[![gitcrawl Demo](assets/demo.gif)](https://asciinema.org/a/dEjZ67TgRJDWUe0y)

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

Running tests
-------------
The test suite covers URL sharding, DOM sanitization, HTML-to-Markdown parsing, Git plumbing, and end-to-end integration:

```sh
make test
```

Usage
-----

```
gitcrawl <command> [options] [arguments]
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
| `version` | Display version information |
| `help` | Display help message |

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

Examples
--------

### 1. Archive a single webpage
```bash
gitcrawl archive https://docs.kernel.org/process/submitting-patches.html
```

### 2. Stream into archive via pipe
```bash
curl -s https://news.ycombinator.com | gitcrawl archive -i https://news.ycombinator.com
```

### 3. Crawl documentation site
```bash
gitcrawl crawl https://docs.kernel.org/process/ -l 2 -p 20 -s
```

### 4. Terminal diff between snapshots
```bash
gitcrawl diff https://docs.kernel.org/process/submitting-patches.html
```

### 5. Fuzzy search historical snapshots
```bash
gitcrawl search "submitting patches" -z
```

### 6. Inspect archived Markdown and Metadata
```bash
gitcrawl show https://docs.kernel.org/process/submitting-patches.html -f md
gitcrawl show https://docs.kernel.org/process/submitting-patches.html -f json
```

### 7. Repack and prune
```bash
gitcrawl gc
```

License
-------
MIT (c) 2026 Ricardo Veronese Ricci
