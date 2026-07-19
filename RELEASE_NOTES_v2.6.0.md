# webbed v2.6.0

A multi-file release. `read_xml` / `read_html` now process a glob or list of files **across
threads** (order-preserving), and schema inference samples **multiple files** by default
instead of only the first — so a column or wider type that appears only in a later file is no
longer missed. Shippable artifacts continue to build against the **DuckDB v1.5.4** release tag.

## Multi-file parallelism (#72)

`read_xml` / `read_xml_objects` / `read_html` / `read_html_objects` now process a glob or list
of files **across threads — one worker per file** — instead of scanning single-threaded
(`MaxThreads()` was hardcoded to 1). Output stays in **file order regardless of thread count**
via DuckDB's batch-index reassembly, so results are deterministic.

- Per-file cursor state (DOM/SAX resources, record index) moved from the global state into a
  new per-thread `XMLReadLocalState`; per-file values like the `filename` column come from
  local state, never a shared cursor.
- Single-file reads are unchanged — one file → one worker → the existing serial path, including
  the 2–4 GiB whole-document path from v2.5.0.
- `parse_*` and `html_extract_tables` operate on single strings and stay single-threaded.

## Multi-file schema inference (#124) — behavior change

`read_xml` / `read_html` over a glob or list **without** `union_by_name` previously inferred the
schema from only the **first file**. A column — or a value that widens a type (an integer column
that has a decimal in file 3) — that first appears in a later file was silently missed.

The default now samples up to **`sample_files`** files (**default 8**) and merges the result,
reusing the same machinery as `union_by_name`:

- `sample_files := 1` restores the exact first-file-only behavior.
- `sample_files := -1` samples every file.
- Ignored when `union_by_name = true` (always scans all files) or when `columns` is given.

**Review before upgrading:** the inferred schema may now include columns / wider types from
files 2–8; and reading `[invalid.xml, valid.xml]` with `ignore_errors` now recovers a real
schema from the valid file rather than falling back to the raw `xml`/`html` column. For file
sets at or below the sample bound, the default now matches `union_by_name` — which remains the
unbounded "scan every file" form (it still differs for globs larger than the bound).

## Deterministic glob order (#72)

Glob matches (e.g. `read_xml('*.xml')`) are now sorted lexicographically — deterministic and
filesystem-independent. This is both the order preserved across threads and the order in which
the first `sample_files` files are chosen for inference. An explicit list of paths keeps the
order you provide.

## Tests

- `test/sql/xml_multifile_parallelism.test` — forces the parallel executor
  (`PRAGMA verify_parallelism`) and asserts exact glob-order output, multiset parity, per-row
  filename attribution, single-file identity, and `_objects` order.
- `test/sql/xml_multifile_schema_inference.test` — written test-first to pin the expected
  behavior and the prior bug (missing later-file column + un-widened type).
- Three existing tests updated to assert the improved default, using `sample_files := 1` as the
  narrow baseline. Full suite green.

## Upgrading

`INSTALL webbed FROM community; UPDATE EXTENSIONS;` once the community-extensions build
publishes. No SQL API removals. The behavior changes are the multi-file schema-inference default
and deterministic glob ordering above — set `sample_files := 1` to keep the previous
first-file-only inference.
