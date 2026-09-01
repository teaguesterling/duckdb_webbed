#!/usr/bin/env python3
"""Execute every SQL example in docs/ against a built binary.

The documentation once described fields (`block_type`, `block_order`) that the
extension has never had, and shipped an `xmltodict` recipe that could not run.
Both survived because nothing ever executed them. This runs them.

Examples that reference a hypothetical table or file are reported separately --
they cannot run without fixtures, and failing on them would make this check a
permanent yellow that everyone learns to ignore. Exit status reflects only
examples that fail against the API we actually ship.
"""
import os, re, subprocess, sys, pathlib

DB = os.environ.get("DUCKDB_BIN", "./build/release/duckdb")
SKIP_DIRS = ("superpowers", "archive")
# A failure naming one of our own symbols is a real defect, even if the message
# also says "does not exist" -- that is the shape a wrong field name takes.
OURS = re.compile(r"(?i)(html_to_duck_blocks|duck_blocks?_to_|html_extract|xml_to_json|"
                  r"element_type|element_order|\bkind\b|block_type|block_order)")
MISSING = re.compile(r"(?i)(does not exist|no files found|IO Error|not found|"
                     r"Table with name|Catalog Error|FROM clause is missing)")
# Network-dependent examples (INSTALL ... FROM community) cannot be verified offline.
ENVIRON = re.compile(r"(?i)(HTTP Error|Connection Error|Failed to download)")
# Examples that need a sibling community extension cannot run without network.
EXTERNAL = re.compile(r"(?i)Catalog Error.*(duck_blocks_to_markdown|markdown_to_duck_blocks|"
                      r"duck_blocks_to_json|read_markdown)")

def examples(path):
    """Yield (line, sql) for each `.. code-block:: sql`.

    The body is the run of lines indented strictly deeper than the directive;
    blank lines belong to it, but a return to the directive's own indent ends
    it. Matching any indent instead swallows the prose that follows the block.
    """
    lines = path.read_text().split("\n")
    for i, line in enumerate(lines):
        m = re.match(r"^(\s*)\.\.\s+code-block::\s*sql\s*$", line)
        if not m:
            continue
        depth, body, j = len(m.group(1)), [], i + 1
        while j < len(lines):
            cur = lines[j]
            if cur.strip() and (len(cur) - len(cur.lstrip())) <= depth:
                break
            body.append(cur)
            j += 1
        text = [b for b in body if b.strip()]
        if not text:
            continue
        pad = min(len(b) - len(b.lstrip()) for b in text)
        sql = "\n".join(b[pad:] for b in body).strip()
        # Skip leading comments before deciding whether this is runnable SQL:
        # most examples here open with a `-- what this does` line, and matching
        # the first character would silently drop them.
        probe = "\n".join(l for l in sql.split("\n")
                          if l.strip() and not l.strip().startswith("--"))
        if re.match(r"(?is)^\s*(select|with|create|install|load|copy|from|attach|pragma|set)", probe):
            yield i + 1, sql

# Tables the HTML/duck_block examples read from. Without these, DuckDB fails on
# the missing table before it ever checks the field names -- which is exactly how
# `block_type` survived in examples that referenced `documents`. Columns are
# deliberately broad so an example naming any of them binds.
PREAMBLE = """
CREATE OR REPLACE TABLE documents AS SELECT * FROM (VALUES
  (1, 'https://example.com/a', '<h1>Title</h1><p>Text</p>', '<root><item>1</item></root>', 'Title')
) t(id, url, html, xml, title);
CREATE OR REPLACE TABLE web_pages AS SELECT * FROM (VALUES
  (1, 'https://example.com/a', '<h1>Title</h1><p>Text</p>', 'Title')
) t(id, url, html, title);
"""

def main():
    if not pathlib.Path(DB).exists():
        print(f"error: no binary at {DB} (build first, or set DUCKDB_BIN)", file=sys.stderr)
        return 2
    docs = [d for d in sorted(pathlib.Path("docs").rglob("*.rst"))
            if not any(s in str(d) for s in SKIP_DIRS)]
    ok, needs_fixture, offline, broken = 0, [], [], []
    for d in docs:
        for line, sql in examples(d):
            try:
                p = subprocess.run([DB, "-unsigned", "-init", "/dev/null",
                                    "-c", PREAMBLE + "\n" + sql],
                                   capture_output=True, text=True, timeout=120)
            except subprocess.TimeoutExpired:
                broken.append((d, line, "timed out")); continue
            if p.returncode == 0:
                ok += 1; continue
            err = ((p.stderr or "") + (p.stdout or "")).strip()
            # Classify on the message alone: DuckDB echoes the failing query, so
            # matching the whole output would see our own symbols in the SQL and
            # call every fixture failure a real defect.
            head = next((l for l in err.split("\n") if l.strip()), "?")[:160]
            if ENVIRON.search(head) or EXTERNAL.search(head):
                offline.append((d, line, head))
            elif MISSING.search(head) and not OURS.search(head):
                needs_fixture.append((d, line, head))
            else:
                broken.append((d, line, head))
    total = ok + len(needs_fixture) + len(offline) + len(broken)
    print(f"doc SQL examples: {total}   ran clean: {ok}   need fixtures: "
          f"{len(needs_fixture)}   offline: {len(offline)}   BROKEN: {len(broken)}")
    for d, line, e in offline:
        print(f"  offline  {d}:{line}  {e}")
    for d, line, e in needs_fixture:
        print(f"  fixture  {d}:{line}  {e}")
    for d, line, e in broken:
        print(f"  BROKEN   {d}:{line}  {e}")
    return 1 if broken else 0

if __name__ == "__main__":
    sys.exit(main())
