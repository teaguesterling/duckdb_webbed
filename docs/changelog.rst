Changelog
=========

v2.4.0 (Current)
----------------

Raises the default type-detection window so common out-of-sample outliers are
caught and widened at inference time instead of erroring — the pragmatic part of
the issue #102 "C" goal, without a DuckDB submodule bump (stays on v1.5.3).

**Behavior changes (review before upgrading)**

- The default ``sample_size`` for ``read_xml`` / ``read_html`` / ``parse_xml`` /
  ``parse_html`` is now **10240** (was 50). A value beyond the first 50 records that
  doesn't fit the type inferred from the earlier rows — e.g. ``'24 495,40 Kč'`` after a
  run of integers — is now seen during inference, so the column widens to VARCHAR and
  the value is preserved, with no options set. Previously such a value aborted the scan
  (or required ``sample_size``/``ignore_errors``/``all_varchar``). (Issue #102)
- The larger window applies to both inference paths, though ``sample_size`` means
  slightly different things in each: the DOM path caps the number of sampled *values
  per field*, while the SAX streaming path caps the number of *records* read into the
  inference prefix. Either way the effective window grows from 50 to 10240, trading a
  larger prefix for correctness on real-world files. ``sample_size`` still overrides it
  per call; ``sample_size := -1`` samples every value on the DOM path (the SAX path
  treats a non-positive value as the finite 50-record fallback).

**Known limitations / next**

- This is the *large-default* form of #102 "C", not unconditional runtime widening: an
  outlier past an explicitly-set, too-small ``sample_size`` still errors (or NULLs under
  ``ignore_errors``). True runtime VARCHAR widening remains the tracked follow-up — the
  ``XmlUncastableValue`` chokepoint is in place (see
  ``test/sql/issue_102_runtime_widening.test.future``).

**Internal**

- ``vcpkg.json`` manifest version → 2.4.0.
- New test fixture ``test/xml/schema_inference/issue_102_large_default.xml`` (60 ints then
  one ``'24 495,40 Kč'`` at position 61) with positive (bare call widens to VARCHAR) and
  negative (``sample_size := 50`` reverts to the clear pre-change error) cases in
  ``test/sql/issue_102_out_of_sample_cast.test``.

v2.3.0
------

Makes ``read_xml`` type detection robust to out-of-sample values, matching
``read_csv``'s recovery instead of aborting the scan. No DuckDB submodule bump
(stays on v1.5.3).

**New Features**

- ``sample_size`` now actually controls type detection. The option existed but was
  ignored — the sniffer hardcoded a 20-value window — so a non-numeric value
  beyond the first 20 was never seen and the column mis-typed. It is now honored by
  ``read_xml`` / ``read_html`` / ``parse_xml`` / ``parse_html`` across every
  detection path. ``sample_size := -1`` samples every value (always-correct
  detection); the default window is 50. (Issue #102)

**Bug Fixes**

- ``read_xml`` no longer hard-fails when a value outside the type-detection sample
  doesn't match the inferred type — e.g. ``'24 495,40 Kč'`` after a run of integers
  raised ``Could not convert string ... to INT32`` and aborted the whole query.
  An out-of-sample / uncastable value now degrades safely: with
  ``ignore_errors := true`` it becomes NULL and the scan continues; otherwise it
  raises a clear, actionable error naming the value, the inferred type, and the
  remedies (``sample_size``, ``all_varchar``, ``ignore_errors``). Applies to
  numeric and ``TIME`` / ``TIME_TZ`` columns. (Issue #102)

**Behavior changes (review before upgrading)**

- A numeric/temporal column with a value beyond the detection sample now widens
  (larger ``sample_size``), NULLs the value (``ignore_errors``), or errors with a
  clear message — previously it aborted with a generic cast error.
- Under ``ignore_errors := true``, a value that cannot be cast to a column's
  inferred type is now skipped (NULL) instead of aborting the file.

**Internal**

- ``vcpkg.json`` manifest version → 2.3.0.

**Known limitations / next**

- The SAX streaming inference path has its own sampling and is unchanged here.
- Runtime VARCHAR widening (preserve an out-of-sample value with no options set,
  like ``read_csv``) is a tracked follow-up; the ``XmlUncastableValue`` chokepoint
  is in place (see ``test/sql/issue_102_runtime_widening.test.future``).

v2.2.1
------

A patch release: the DuckDB-WASM build now loads, plus a SAX streaming
attribute-parity fix. No DuckDB submodule bump (stays on v1.5.3).

**Bug Fixes**

- Fixed the DuckDB-WASM build installing but failing to load — libxml2 (and its
  ``zlib`` dependency) were not linked into the ``emcc -sSIDE_MODULE=2`` side
  module, so their symbols were left unresolved and the ``.wasm`` failed to load.
  ``target_link_libraries`` is ignored by that separate emcc link step; both
  archives are now passed via ``LINKED_LIBS``. (Issue #96)
- Fixed SAX streaming dropping the attributes of a record's repeated nested child
  elements — a ``LIST<STRUCT(@attr…)>`` streamed back as ``[NULL, NULL]`` while the
  DOM path returned the attribute values. Each direct child's own attributes are
  now carried through the fragment extractor, restoring DOM/SAX parity.
  (Issues #97, #98)

**Testing**

- Added a ``test/wasm/`` regression suite wired into CI as the ``wasm-load-test``
  job: a runtime-free static symbol check (hard gate — libxml2/zlib must be linked
  into the side module, not left unresolved) plus a duckdb-wasm live load test.
  The reusable distribution workflow builds the ``.wasm`` but never loads it, which
  is why #96 shipped green.
- Added ``test/sql/sax_nested_child_attr_parity.test`` (DOM/SAX parity for the
  repeated-nested-attribute case); native suite at 83 cases / 2875 assertions.

**Internal**

- ``vcpkg.json`` manifest version → 2.2.1.

v2.2.0
------

A large correctness and security release: memory-safety, injection, and
denial-of-service fixes across the scalar functions and parsers, ``read_xml``
schema-inference fixes, faithful ``parse_html`` whitespace handling, and
forward-compatibility with current DuckDB ``main``. No DuckDB submodule bump.

**Security**

- Vector memory safety — twelve scalar functions plus ``ConvertList/StructToXML``
  indexed raw ``FlatVector`` data by row, corrupting the heap on NULL, constant, or
  dictionary-encoded inputs; they now use ``UnifiedVectorFormat`` with NULL-in/NULL-out
  semantics (Issue #86)
- Fixed markup injection in ``xml_wrap_fragment`` — the wrapper name is now validated
  with ``xmlValidateName`` plus an embedded-NUL guard (Issue #89)
- Fixed markup injection in ``to_xml`` — the ``node_name`` argument and STRUCT field
  names are now validated before being emitted into ``xml``-typed output (Issue #93)
- Fixed memory disclosure in ``read_xml`` — with ``ignore_errors:=true`` and the
  single-column fallback schema, rows left output slots uninitialized and returned
  stale heap bytes; a shared ``EmitRow`` helper NULL-fills every column (Issue #87)
- Fixed denial of service in ``duck_blocks_to_html`` — malformed Pandoc-table JSON
  could hang the parser indefinitely; the parse loops now guarantee forward progress
  and no longer read past the end of truncated input (Issue #90)

**Bug Fixes**

- Fixed valid XML mislabeled invalid under ``threads > 1`` — ``IsValidXML`` gated on a
  process-global parse-error flag; validity is now decided per-thread (Issue #83)
- Distinguished libxml2 out-of-memory from malformed input — a transient
  ``XML_ERR_NO_MEMORY`` was reported as invalid XML and dropped under ``ignore_errors``;
  it now raises ``OutOfMemoryException`` (propagated through ``XMLDocRAII`` moves)
  (Issues #84, #94)
- Fixed ``parse_html`` whitespace handling — significant inline whitespace was deleted
  and ``script``/``style``/CDATA content mangled; whitespace is now normalized on the
  parse tree, preserving ``pre``/``textarea``/``script``/``style``/CDATA verbatim
  (Issues #88, #91)

**read_xml Schema Inference** (Issue #87)

- Integer columns now widen INTEGER → BIGINT → DOUBLE by value range (was INT32, then
  threw at extract time)
- ``attr_mode:='prefixed'`` columns now extract correctly (prefix stripped on lookup)
- DESCRIBE column order is now deterministic (first-seen document order, DOM and SAX)
- Locale-independent numeric parsing via strict ``TryCast``; ``inf``/``nan`` infer VARCHAR

**Compatibility**

- DuckDB ``main`` ``duckdb::Identifier`` change absorbed via ``duckdb_compat.hpp``
  (no-op against the pinned v1.5.3 build) (Issue #92)

**Testing**

- 2866 assertions across 82 test cases; new ``vector_safety``, injection-validation,
  schema-inference, ``parse_html_whitespace``, robustness, concurrency-race, and
  out-of-memory suites

**Internal**

- ``vcpkg.json`` manifest version → 2.2.0

v2.1.1
------

**Bug Fixes**

- Fixed ``xml_to_json`` emitting malformed JSON for values containing quotes or C0
  control characters, so output could not round-trip through ``CAST(... AS JSON)``;
  a new ``EscapeJSONString`` helper escapes ``"``, ``\``, and the C0 range per RFC 8259
  while leaving valid raw characters and UTF-8 untouched (Issue #78)

**Compatibility**

- Restored ``duckdb-next-build`` against current DuckDB ``main`` (private ``bind_info``,
  ``ForceMaxLogicalType`` signature change, strict named-argument matching); all changes
  are no-ops against the pinned v1.5.3 build

**Internal**

- ``vcpkg.json`` manifest version → 2.1.1

v2.1.0
------

**New Features**

- STRUCT widening in ``read_xml(union_by_name:=true)`` — cross-file STRUCT shape
  disagreements previously collapsed to VARCHAR; ``MergeXMLColumnType`` now recursively
  unions STRUCT fields and LIST element types, reconciles scalar-vs-``LIST`` cardinality,
  and infers a repeated element's fields from all occurrences in document order (Issue #75)
- SAX streaming path now produces rich types and participates in ``union_by_name``
  widening, with document-order fragment accumulation and namespace-declaration handling
  (Issues #75, #77, #80)

**Changes**

- ``duckdb-stable-build`` CI matrix bumped to DuckDB v1.5.3; ``duckdb`` submodule →
  ``14eca11bd9``, ``extension-ci-tools`` → ``4b3b37b0`` (Issue #79)

**Compatibility**

- ``duckdb_compat.hpp`` keeps ``duckdb-next-build`` green against DuckDB ``main``
  (per-vector buffer sizing, StructVector/ListVector API changes) (Issue #76)
- MSVC build fix for DuckDB's vendored ``fmt`` (C++17 inline variables)

**Bug Fixes**

- Mixed text/XML document order and prefixed names under ``namespaces:='keep'`` are now
  preserved through the SAX path (Issue #77)

**Internal**

- ``vcpkg.json`` manifest version → 2.1.0

v2.0.1
------

**Bug Fixes**

- Fixed whitespace collapsing in text content — ``read_xml`` collapsed internal
  whitespace (newlines, tabs, multi-space runs) into a single space, destroying
  semantically meaningful structure in CDATA sections, source code, and multi-line
  content (Issue #73)

**New Parameter**

- Added ``preserve_whitespace`` (BOOLEAN, default ``true``) to ``read_xml`` and
  ``read_html``. Default trims leading/trailing whitespace, normalizes CRLF/CR to LF per
  XML 1.0 §2.11, and preserves internal whitespace as-is; ``false`` restores the previous
  collapsing behavior

**Compatibility**

- Added compatibility layer for upcoming DuckDB API breaking changes (bind function
  signature, private ScalarFunction fields, vector header relocations)

**Internal**

- ``vcpkg.json`` manifest version → 2.0.1

v2.0.0
------

**New Features**

- SAX-based streaming parser for very large XML files — files exceeding
  ``maximum_file_size`` are automatically parsed using SAX mode, reducing peak
  memory from ~4x file size (DOM) to proportional to a single record (Issue #68)

  - New ``streaming`` parameter (default: ``true``). When enabled, oversized XML
    files are streamed via libxml2's SAX push parser in 64KB chunks instead of
    building a full DOM tree. Set ``streaming:=false`` to restore the previous
    behavior of erroring on oversized files.
  - SAX mode supports simple tag-name ``record_element`` values (e.g., ``'item'``).
    XPath expressions automatically fall back to DOM parsing.
  - Not available for HTML files (libxml2 HTML parser is DOM-only).

**Changes**

- Reduced default ``maximum_file_size`` from 128MB to 16MB. With SAX streaming
  enabled by default, this threshold now controls when to switch from DOM to SAX
  rather than when to reject files. Files above 16MB are streamed automatically.
  Set ``maximum_file_size`` higher to use DOM for larger files, or set
  ``streaming:=false`` to error on oversized files (previous behavior).

**Limitations**

- SAX mode currently handles flat records (scalars, attributes, repeated elements).
  Nested STRUCT extraction from SAX events is not yet implemented — deeply nested
  records fall back to raw XML string values.

**Testing**

- 68 test suites, 2511 assertions
- Comprehensive DOM/SAX equivalence tests covering type inference, datetime_format,
  record_element, cross-record attribute discovery, large row counts (3000 rows
  across chunk boundaries), UTF-8 content, and nullstr interaction
- Stress tested with 382MB file (1M records): zero data loss, 5x faster than DOM,
  184x less memory (25MB vs 4.6GB peak)

v1.5.0
-----------------

**New Features**

- Added ``datetime_format`` parameter to ``read_xml``, ``read_html``, ``parse_xml``, and
  ``parse_html`` for controlling date/time detection and parsing — supports preset names
  (``auto``, ``none``, ``us``, ``eu``, ``iso``, etc.), custom strftime format strings, and
  lists of formats. Replaces regex-based temporal detection with DuckDB's ``StrpTimeFormat``
  candidate elimination approach (Issue #38)
- Added ``nullstr`` parameter for custom NULL value representation (Issue #40)
- Lazy DOM extraction for reduced peak memory — records are now extracted one at a time
  directly from the DOM instead of caching all rows at once (Issue #17, Phase 1)
- Type inference for elements with attributes — ``#text`` field now infers proper types
  (DOUBLE, INTEGER, DATE, BOOLEAN) instead of defaulting to VARCHAR (Issues #49, #46)

**Improvements**

- Increased default ``maximum_file_size`` from 16MB to 128MB (Issue #66)

**Bug Fixes**

- Fixed ``read_xml`` returning NULL for non-Latin text content — Cyrillic, CJK, and other
  multi-byte UTF-8 characters were being stripped by whitespace trimming (Issue #64)

v1.4.0
------

**New Features**

- Added ``parse_xml(content)`` table function to parse XML strings with schema inference
- Added ``parse_xml_objects(content)`` table function to parse XML strings and return raw XML type
- Added ``parse_html(content)`` table function to parse HTML strings with schema inference
- Added ``parse_html_objects(content)`` table function to parse HTML strings and return raw HTML type

**Bug Fixes**

- Fixed CDATA sections being converted to empty objects in ``xml_to_json`` (Issue #63)

v1.3.3
------

**Bug Fixes**

- Fixed table blocks rendering to HTML (Issue #62)

**Testing**

- Added comprehensive HTML ↔ Duck Block conversion tests

v1.3.2
------

**New Features**

- Added ``filename`` parameter to ``read_xml`` and ``read_html`` functions

**Documentation**

- Fixed high priority documentation issues
- Added documentation badge linking to readthedocs

v1.3.1
------

**Bug Fixes**

- Fixed ``duck_blocks_to_html()`` outputting literal "NULL" for parent elements with NULL content (parent blocks with inline children)

v1.3.0
------

**New Features**

- Added ``html_to_duck_blocks`` function to convert HTML into structured document blocks
- Added ``duck_blocks_to_html`` function to convert document blocks back to HTML
- Added namespace parameter to XPath scalar functions (``xml_extract_text``, ``xml_extract_elements``, etc.)
- Added ``xml_lookup_namespace(prefix)`` to look up common namespace URIs
- Added ``xml_find_undefined_prefixes(xml, xpath)`` to detect undeclared namespace prefixes
- Added implicit casting from XML/HTML types to VARCHAR, enabling string functions on XML/HTML values

**Bug Fixes**

- Fixed UTF-8 encoding in ``html_extract_text`` - characters like "chère" are now correctly preserved (Issue #53)
- Fixed documentation mismatches between README and actual function behavior (Issue #54)
- Added regression tests for ``xml_extract_attributes`` segfault report (Issue #55)

**Documentation**

- Added comprehensive XPath namespace handling documentation with ``local-name()`` examples
- Updated test statistics: 58 test suites, 1901 assertions
- Added documentation for ``html_escape`` and ``html_unescape`` functions
- Created Read the Docs documentation structure

**New Test Coverage**

- Added test suite for namespace handling patterns (Issue #4)
- Added test suite for batch file processing (Issue #17)
- Added tests for UTF-8 encoding with various character sets

v1.2.0
------

**New Features**

- Added ``union_by_name`` parameter for combining files with different schemas
- Added ``all_varchar`` parameter for forcing VARCHAR types
- Added ``force_list`` parameter for ensuring LIST types

**Bug Fixes**

- Fixed cross-record attribute discovery for nested elements (Issue #50)
- Fixed LIST extraction and record element serialization
- Fixed schema consistency for multi-file reads

**Improvements**

- Enhanced thread safety with per-operation configuration (Issue #7)
- Improved error handling for malformed documents

v1.1.0
------

**New Features**

- Added ``read_html`` and ``read_html_objects`` functions
- Added HTML table extraction functions
- Added ``html_extract_links`` and ``html_extract_images``
- Added ``xml_to_json`` with comprehensive options

**Improvements**

- Improved schema inference for complex nested structures
- Better handling of repeated elements
- Enhanced type detection for dates and timestamps

v1.0.0
------

**Initial Release**

- Core XML parsing with libxml2
- ``read_xml`` and ``read_xml_objects`` functions
- XPath extraction functions
- XML validation and formatting utilities
- Basic schema inference
