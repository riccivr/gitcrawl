#!/usr/bin/env python3
import json
import subprocess
import time
import os
import shutil

CAST_FILE = "assets/demo.cast"
GIF_FILE = "assets/demo.gif"
DEMO_REPO = "/tmp/gitcrawl_demo_repo"

os.makedirs("assets", exist_ok=True)
shutil.rmtree(DEMO_REPO, ignore_errors=True)
os.makedirs(DEMO_REPO, exist_ok=True)

# Build binaries first
subprocess.run(["make", "all"], check=True)

HTML_V1 = """<!DOCTYPE html>
<html>
<head><title>Kernel Patch Submission Guidelines</title><script>var csrf = 'nonce-123';</script></head>
<body>
<h1>Submitting Patches: The Essential Guide to Getting Your Code into the Kernel</h1>
<p>For a person or company who wishes to submit a change to the Linux kernel, the process can sometimes be daunting if you're not familiar with the system.</p>

<h2>1. Obtain a Current Source Tree</h2>
<p>Ensure you are working against the latest linux-next or subsystem tree before developing patches.</p>

<h2>2. Code Style and Conventions</h2>
<ul>
  <li>Follow the kernel coding style in <code>Documentation/process/coding-style.rst</code>.</li>
  <li>Keep functions short and focused on a single task.</li>
  <li>Use 8-character tab stops for indentation.</li>
</ul>

<h2>3. Sign Your Work - The Developer's Certificate of Origin</h2>
<p>Add a <code>Signed-off-by: Random J Developer &lt;random@developer.example.org&gt;</code> line to every patch.</p>
</body>
</html>"""

HTML_V2 = """<!DOCTYPE html>
<html>
<head><title>Kernel Patch Submission Guidelines</title><script>var csrf = 'nonce-999';</script></head>
<body>
<h1>Submitting Patches: The Essential Guide to Getting Your Code into the Kernel</h1>
<p>For a person or company who wishes to submit a change to the Linux kernel, the process can sometimes be daunting if you're not familiar with the system.</p>

<h2>1. Obtain a Current Source Tree</h2>
<p>Ensure you are working against the latest linux-next or subsystem tree before developing patches.</p>

<h2>2. Code Style and Conventions</h2>
<ul>
  <li>Follow the kernel coding style in <code>Documentation/process/coding-style.rst</code>.</li>
  <li>Keep functions short and focused on a single task.</li>
  <li>Use 8-character tab stops for indentation.</li>
  <li>Verify patches pass checkpatch.pl before mailing to the subsystem maintainer.</li>
</ul>

<h2>3. Sign Your Work - The Developer's Certificate of Origin</h2>
<p>Add a <code>Signed-off-by: Random J Developer &lt;random@developer.example.org&gt;</code> line to every patch.</p>

<h2>4. Email Client Configuration</h2>
<p>Use <code>git send-email</code> to avoid whitespace mangling and quote wrapping from webmail clients.</p>
</body>
</html>"""

with open("/tmp/doc_v1.html", "w") as f:
    f.write(HTML_V1)

with open("/tmp/doc_v2.html", "w") as f:
    f.write(HTML_V2)

events = []
current_time = 0.0

def add_event(delta, text):
    global current_time
    current_time += delta
    events.append([round(current_time, 3), "o", text])

def type_command(cmd, prompt="riccivr@workstation:~$ ", typing_speed=0.025):
    add_event(0.2, prompt)
    for ch in cmd:
        add_event(typing_speed, ch)
    add_event(0.12, "\r\n")

def run_and_record(cmd_display, shell_cmd, prompt="riccivr@workstation:~$ ", pause_after=2.0):
    type_command(cmd_display, prompt)
    proc = subprocess.run(shell_cmd, shell=True, capture_output=True, text=True)
    out = proc.stdout
    if proc.stderr:
        out += proc.stderr
    out_crlf = out.replace("\r\n", "\n").replace("\n", "\r\n")
    add_event(0.04, out_crlf)
    if not out_crlf.endswith("\r\n"):
        add_event(0.01, "\r\n")
    add_event(pause_after, "")

# Initial screen clear
add_event(0.0, "\x1b[2J\x1b[H")

# 1. Version check
run_and_record("gitcrawl version", "./gitcrawl version", pause_after=1.2)

# 2. Archive snapshot 1
run_and_record("cat /tmp/doc_v1.html | gitcrawl archive -d " + DEMO_REPO + " -i https://docs.kernel.org/process/submitting-patches.html",
               "cat /tmp/doc_v1.html | ./gitcrawl archive -d " + DEMO_REPO + " -i https://docs.kernel.org/process/submitting-patches.html",
               pause_after=1.8)

# 3. Show archived markdown
run_and_record("gitcrawl show -d " + DEMO_REPO + " https://docs.kernel.org/process/submitting-patches.html -f md",
               "./gitcrawl show -d " + DEMO_REPO + " https://docs.kernel.org/process/submitting-patches.html -f md",
               pause_after=2.2)

# 4. Archive snapshot 2 (content update)
run_and_record("cat /tmp/doc_v2.html | gitcrawl archive -d " + DEMO_REPO + " -i https://docs.kernel.org/process/submitting-patches.html -m 'docs: update guidelines with checkpatch and git send-email'",
               "cat /tmp/doc_v2.html | ./gitcrawl archive -d " + DEMO_REPO + " -i https://docs.kernel.org/process/submitting-patches.html -m 'docs: update guidelines with checkpatch and git send-email'",
               pause_after=1.8)

# 5. Diff between historical snapshots
run_and_record("gitcrawl diff -d " + DEMO_REPO + " https://docs.kernel.org/process/submitting-patches.html",
               "./gitcrawl diff -d " + DEMO_REPO + " https://docs.kernel.org/process/submitting-patches.html",
               pause_after=2.4)

# 6. Fuzzy search archived historical pages
run_and_record("gitcrawl search -d " + DEMO_REPO + " -z 'submitting patch'",
               "./gitcrawl search -d " + DEMO_REPO + " -z 'submitting patch'",
               pause_after=2.0)

# 7. Commit history log
run_and_record("gitcrawl log -d " + DEMO_REPO + " https://docs.kernel.org/process/submitting-patches.html",
               "./gitcrawl log -d " + DEMO_REPO + " https://docs.kernel.org/process/submitting-patches.html",
               pause_after=2.2)

# Final prompt
add_event(0.2, "riccivr@workstation:~$ ")
add_event(1.5, "")

header = {
    "version": 2,
    "width": 96,
    "height": 30,
    "timestamp": int(time.time()),
    "env": {"SHELL": "/bin/bash", "TERM": "xterm-256color"},
    "title": "gitcrawl demo"
}

with open(CAST_FILE, "w") as f:
    f.write(json.dumps(header) + "\n")
    for ev in events:
        f.write(json.dumps(ev) + "\n")

print(f"Generated {CAST_FILE} ({len(events)} events, {round(current_time, 2)}s)")

# Render with agg
agg_cmd = f"agg --theme monokai --font-size 14 --fps-cap 30 {CAST_FILE} {GIF_FILE}"
print(f"Running: {agg_cmd}")
subprocess.run(agg_cmd, shell=True, check=True)
print(f"Rendered {GIF_FILE} successfully!")
