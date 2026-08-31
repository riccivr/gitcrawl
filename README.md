# gitcrawl

> Git-native web archival engine and crawler in clean C99.

`gitcrawl` archives web pages and commits their history straight into Git repositories as a content-addressable object store.

Based on the architecture described in [*Preserving the Web with Git*](https://riccivr.github.io/blog/post.html?post=preserving-the-web-with-git).

---

## Features

- **Direct-to-Object Ingestion**: Bypasses the working tree by writing blobs, trees, and commits directly to `.git/objects` via Git plumbing.
- **Two-Tier Storage**:
  - `index.md`: Clean, sanitized Markdown for crisp, human-readable `git diff` outputs.
  - `index.html.gz`: Compressed original HTML payload intact.
  - `metadata.json`: HTTP status, headers, timestamps, and cryptographic hashes.
- **Noise Sanitization**: Automatically strips CSRF tokens, session cookies, dynamic tracking pixels, ad banners, and timestamp noise before diff generation.
- **URL Sharding**: Automatically fragments hierarchical URLs into clean directory structures with query parameter normalization.
- **Fuzzy History Exploration**: Fast fuzzy search algorithms inspired by [`approx`](https://github.com/riccivr/approx) to find historical pages from the shell.
- **Zero Runtime Dependencies**: Written in standard POSIX C99 with standard zlib.

---

## Pipeline

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

---

## Installation

```bash
git clone https://github.com/riccivr/gitcrawl.git
cd gitcrawl
make
sudo make install
```

---

## Usage

### 1. Archive a single webpage
```bash
gitcrawl archive https://docs.kernel.org/process/submitting-patches.html
```

### 2. Archive from stdin stream
```bash
curl -s https://news.ycombinator.com | gitcrawl archive -i https://news.ycombinator.com
```

### 3. Crawl a documentation site
```bash
gitcrawl crawl https://docs.example.com/api --depth 2 --max-pages 25 --same-domain
```

### 4. Inspect clean terminal diffs
```bash
gitcrawl diff https://docs.kernel.org/process/submitting-patches.html
```

### 5. Search archived historical pages with fuzzy matching
```bash
gitcrawl search "submitting patches" --fuzzy
```

### 6. Display archived contents
```bash
# Print clean Markdown
gitcrawl show https://docs.kernel.org/process/submitting-patches.html --format md

# Print metadata JSON
gitcrawl show https://docs.kernel.org/process/submitting-patches.html --format json
```

### 7. Optimize Git storage
```bash
gitcrawl gc
```

---

## License

MIT (c) 2026 Ricardo Veronese Ricci
