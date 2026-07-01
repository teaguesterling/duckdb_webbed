# webbed v2.4.0

A feature-and-robustness release: `read_xml` type detection now catches out-of-sample
outliers by default, the XML serializers emit consistent literal-UTF-8 / DOM-parity
output, whole-document reads work past the old 2 GiB barrier, and the stable build moves
to **DuckDB v1.5.4**.

## `read_xml` type detection catches out-of-sample outliers by default (#102)

The default `sample_size` for `read_xml` / `read_html` / `parse_xml` / `parse_html` is now
**10240** (was 50). A value beyond the first rows that doesn't fit the type inferred from
the earlier ones — e.g. `'24 495,40 Kč'` after a run of integers — is now seen during
inference, so the column widens to VARCHAR and the value is preserved **with no options
set**, instead of aborting the scan. This is the pragmatic form of the issue #102 "C" goal
(true unbounded runtime widening isn't possible for a table function, which can't change
its return types after `bind`).

- `sample_size` still overrides the default per call; `sample_size := -1` samples every
  value on the DOM path (the SAX streaming path treats a non-positive value as a finite
  50-record fallback).
- An outlier past an explicitly-set, too-small `sample_size` still errors (or NULLs under
  `ignore_errors := true`) — true runtime VARCHAR widening remains a tracked follow-up.

## Large XML/HTML documents — 2 GiB to 4 GiB (#103, PR #112)

`read_xml` / `read_xml_objects` / `read_html` failed on documents larger than ~2 GiB (e.g.
the ~2.8 GiB Czech justice bulk register) with `Invalid argument` on macOS or
`contains invalid XML` on Linux, even with `maximum_file_size` raised above the file size.
Two independent ~2 GiB barriers are fixed:

1. **Whole-file read.** A single `FileHandle::Read` of >2 GiB returns `EINVAL` on macOS and
   short-reads on Linux. Reads are now chunked (`ReadFileFully`, ≤1 GiB per call), advancing
   by the bytes actually returned and erroring only on a non-positive return.
2. **DOM/HTML parse.** `xmlCtxtReadMemory` / `htmlReadMemory` take the buffer length as an
   `int`, so a >INT_MAX buffer wrapped negative and libxml2 rejected it as malformed —
   which is why fixing only the read wasn't enough. `XMLDocRAII` now parses via the IO-based
   `xmlCtxtReadIO` / `htmlReadIO`, fed from the in-memory document through a callback.

No `XML_PARSE_HUGE`: the `int` parameter was the only barrier, so the libxml2 DoS limits
from the v2.2.0 security hardening stay in place. The new ceiling is DuckDB's **4 GiB
single-value cap** (`string_t` length is `uint32`); genuinely larger inputs must use
record-level streaming via `read_xml`. (Read fix based on @onnimonni's #103.)

## Serialization consistency: literal UTF-8 and DOM/SAX parity

Three fixes bring the serializers into agreement so the same node produces the same bytes
regardless of which path produced it:

- **Fragment serializers** (`xml_extract_elements(...)` / `XMLFragment::VARCHAR`) now emit
  non-ASCII text as **literal UTF-8** instead of escaping every non-ASCII character to a
  numeric character reference (`ö` → `&#xF6;`), matching the `read_xml` reader capture and
  `xml_extract_text`. (Issue #108, PR #110 by @marcel-more)
- **`to_xml` / `xml`** (and the `LIST` / `STRUCT` forms) likewise keep non-ASCII text
  literal. As a result the XML declaration they emit now states the encoding explicitly —
  `<?xml version="1.0" encoding="UTF-8"?>` instead of `<?xml version="1.0"?>`. (Follow-up to
  #108 / #110.)
- **SAX streaming** now serializes control characters in captured raw XML the same way DOM
  does: a carriage return becomes `&#13;` in text content, and CR/LF/TAB become
  `&#13;` / `&#10;` / `&#9;` in attribute values (XML 1.0 §2.11 / §3.3.3). Previously the SAX
  path emitted a raw CR byte, so a captured-subtree VARCHAR was not byte-identical across
  reader modes and could silently break downstream `\s`-based text processing.
  (Issue #109, PR #111 by @marcel-more)

## DuckDB v1.5.4 (#107)

The stable build moves from DuckDB v1.5.3 to **v1.5.4** (`duckdb` submodule → `08e34c4`,
`extension-ci-tools` → `b777c70`), with the `duckdb-stable-build` CI pins and WASM artifact
names bumped to match, resolving the missing v1.5.4 community-extensions build. The
`duckdb-next-build` (DuckDB `main`) job is unchanged.

## Behavior changes (review before upgrading)

- A numeric/temporal column with a value beyond the (now larger) detection sample widens to
  VARCHAR, NULLs the value under `ignore_errors`, or errors with a clear message.
- `xml_extract_elements(...)::VARCHAR`, `to_xml`/`xml`, and the SAX capture path change their
  string output: non-ASCII is now literal UTF-8, and `to_xml`/`xml` now emit
  `<?xml version="1.0" encoding="UTF-8"?>`. Consumers that exact-string-match this output
  should update.

## Acknowledgments

Serialization fixes #110 (#108) and #111 (#109) are @marcel-more's; the >2 GiB read fix
builds on @onnimonni's #103. Thank you.
