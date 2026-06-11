# v2.2.0 — Security Hardening, Robustness & Schema-Inference Fixes

A large correctness and security release: a sweep of memory-safety, injection, and denial-of-service fixes across the scalar functions and parsers, five `read_xml` schema-inference fixes, faithful `parse_html` whitespace handling, and forward-compatibility with current DuckDB `main`. Most of this release is @bendrucker's work (#83, #86, #87, #89, #90). No DuckDB submodule bump (stays on v1.5.3).

## Security

- **Vector memory safety (#86).** Twelve scalar functions plus `ConvertList/StructToXML` indexed raw `FlatVector::GetData` by row, corrupting the heap (`SIGABRT: pointer being freed was not allocated`) on NULL, constant, or dictionary-encoded inputs. They now go through `UnifiedVectorFormat` with NULL-in/NULL-out semantics.
- **Markup injection in `xml_wrap_fragment` (#89).** The wrapper name was concatenated into markup unvalidated (`xml_wrap_fragment('x', 'a><evil')`). It is now validated with `xmlValidateName` plus an embedded-NUL guard.
- **Markup injection in `to_xml` (#93).** The `node_name` argument **and STRUCT field names** were emitted into trusted `xml`-typed output unvalidated (`to_xml(42, 'a><evil')`, `to_xml({'a><evil': 1})`). Both are now validated; namespace-prefixed names still work.
- **Memory disclosure in `read_xml` (#87).** With `ignore_errors := true` and the single-column fallback schema, emitted rows left output slots uninitialized and returned stale heap bytes (a multi-thousand-character garbage string). A shared `EmitRow` helper NULL-fills every unprovided column.
- **Denial of service in `duck_blocks_to_html` (#90).** Malformed Pandoc-table JSON could hang the hand-rolled parser indefinitely; the object-key, alignments, and value-skip loops now guarantee forward progress, and truncated input no longer reads past the end.

## Correctness

- **Concurrency: valid XML mislabeled invalid (#83).** `IsValidXML` gated on a process-global parse-error flag, so under `threads > 1` one thread's malformed parse could flip another thread's check of a valid document — aborting the scan or silently dropping rows. Validity is now decided from each thread's own parse result.
- **libxml2 OOM vs malformed input (#84 / #94).** A transient allocation failure (`XML_ERR_NO_MEMORY`) was reported as `File X contains invalid XML` and dropped under `ignore_errors`. It is now distinguished and raised as `OutOfMemoryException`. (Includes a follow-up so the resource-failure flag propagates through `XMLDocRAII`'s move operations on the DOM read path.)
- **`parse_html` whitespace (#88 / #91).** The minifier deleted significant inline whitespace (`<p>Hello <b>world</b> again</p>` lost the space before "again") and, by tracking `<`/`>` over serialized output, mangled or inconsistently preserved `<script>`/`<style>`/CDATA content depending on whether it contained a literal `>`. It now normalizes whitespace on the parse tree: runs collapse to a single space in text nodes, while CDATA sections, comments, and `pre`/`textarea`/`script`/`style` content are preserved verbatim. Also fixes `std::isspace` undefined behavior on UTF-8 bytes.
- **`duck_blocks_to_html` malformed/NULL input (#90).** Unguarded `std::stoi` on `heading_level` now degrades to the default; NULL struct fields and attribute values no longer leak the literal string `"NULL"` into output.

## `read_xml` schema inference (#87)

- **Integer width.** `<n>3000000000</n>` inferred INT32 and then threw at extract time. Samples now widen INTEGER → BIGINT → DOUBLE by value range, like the CSV sniffer.
- **`attr_mode := 'prefixed'`.** Prefixed attribute columns (`@id`) inferred but extracted as NULL; the prefix is now stripped before attribute lookup (also fixes underscore-containing attribute names in normal mode).
- **Column order.** `IdentifyColumns` returned an `unordered_map`, making DESCRIBE order non-deterministic. Columns now follow first-seen document order, for both DOM and SAX.
- **Locale and non-finite values.** `IsInteger`/`IsDouble` used locale-dependent `std::stod` and accepted `inf`/`nan`; they now use strict `TryCast` and reject non-finite doubles (such columns infer VARCHAR).

## DuckDB main compatibility (#92)

DuckDB `main` introduced `duckdb::Identifier`, replacing `std::string` as the key of `child_list_t` (STRUCT/UNION field names) and several name-typed fields. `duckdb_compat.hpp` gains `CompatIdentifierName` / `CompatMakeIdentifier`, gated on header presence, restoring the `duckdb-next` CI. No-op against the pinned v1.5.3 build.

## Behavior changes (review before upgrading)

- `read_xml` integer columns with values > INT32 now infer **BIGINT** (were INT32 → runtime error); `inf`/`nan` infer **VARCHAR**; DESCRIBE column order is now **deterministic**; `'12abc'` against an explicit INTEGER column now **errors** instead of truncating to `12` (matches DuckDB cast semantics).
- `to_xml` now **errors** when `node_name` or a STRUCT field name is not a valid XML element name (previously produced malformed/injected output).
- `parse_html` **collapses** inter-element whitespace to a single space (previously deleted it) and preserves `pre`/`script`/`style`/CDATA verbatim.
- A libxml2 out-of-memory condition now **propagates even under `ignore_errors := true`** rather than skipping the file.

## Test coverage

New suites: `vector_safety`, `xml_wrap_fragment_validation`, `to_xml_name_injection`, `xml_schema_inference_fixes`, `parse_html_whitespace`, `duck_block_robustness`, `concurrency_invalid_xml_race`. The suite is now **2864 assertions across 81 test cases**.

## Acknowledgments

The bulk of this release is @bendrucker's — #83, #86, #87, #89, #90 (and #84, completed here). Thank you.
