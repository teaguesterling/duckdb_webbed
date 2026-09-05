Changelog
=========

Unreleased
----------

**Behaviour change: attribute capture is now a parameter, and** ``class`` **is opt-in.**

``read_html_blocks`` kept ``id`` and ``class`` on ``div``/``section``, kept only ``id`` on
headings, and dropped both on everything else (#142). There were no tiers -- three copy sites
written at different times -- and the writer mirrored them, so read-then-write silently
destroyed attributes the source had.

- ``capture_attributes := 'default' | 'classes' | '*' | true | false | [...]`` on
  ``html_to_duck_blocks``, ``read_html_blocks`` and ``parse_html_blocks`` governs which source
  attributes are copied onto **every** element, block and inline. Default:
  ``['id', 'name', 'href', 'src']``.
- ``<a name="...">`` -- the pre-HTML5 anchor -- is now captured as ``name``. It was dropped
  outright before, so a legacy anchor was indistinguishable from a bare ``<a>``.
- ``duck_blocks_to_html`` renders captured attributes back on every element, and an anchor
  with no ``href`` no longer renders ``href=""``.
- **``class`` is no longer captured by default** -- it is opt-in via ``'classes'``. This
  changes ``div``, ``section`` and ``span``, which captured it unconditionally before.
  Downstream, **HTML → Pandoc AST conversion through** ``duck_block_utils`` **loses class
  unless requested**, because Pandoc encodes semantic structure in classes.
- Vocabulary keys (``role``, ``heading_level``, ...) are reserved: never copied from the
  source, so ``'*'`` cannot forge them.

**``filename`` on** ``read_html_blocks`` **is now trailing, and takes core's forms.**

- The column now comes **after** ``element_order``, not first. duck_block spec 6.4 keys
  8-field acceptance on the exact type -- seven canonical fields, then ``filename VARCHAR`` --
  so the old leading position (``STRUCT(filename, kind, ...)``) did not bind against
  ``duck_block_utils`` at all. The emitted type string is now asserted in the suite.
- ``filename := true`` adds a ``filename`` column. ``filename := 'src_path'`` adds it under
  that name, as ``read_csv``/``read_json``/``read_parquet`` do -- **but a renamed column is
  not the accepted 8-field type and will not bind as duck_blocks** (``list(b)`` into any
  ``duck_block_utils`` function fails). This is the one place matching core diverges from
  the vocabulary, which prefers the boolean form; use ``true`` when the rows feed
  ``duck_block_utils``.
- ``include_filepath`` and ``file_path`` are deprecated aliases, kept for one release.

v2.8.2
------

A bugfix release for the ``duck_block`` reader and writer. Behavioural
corrections against a spec that moved -- no new public surface, so the function
list is unchanged.

**Fixed**

- **Containment.** ``html_to_duck_blocks()`` selected blocks with an XPath
  descendant query, which cannot express containment. ``<blockquote>`` and
  ``<figure>`` emitted their children *and* themselves; ``<dl>`` produced zero
  blocks; ``<section>`` and ``<details>`` flattened; ``<img>`` inside ``<p>``
  emitted twice; ``<td>`` and ``<li>`` content duplicated. The XPath is replaced
  by a recursive walk with a container/leaf distinction.
- **Dropped content.** ``<title>`` and ``<meta>`` were dropped entirely; nested
  lists corrupted text (``["outerinner"]``); ``<ol start>`` was ignored; loose
  text was dropped in mixed containers.
- **Inline formatting.** Unmapped inline elements lost their identity, and
  nested formatting emitted ``<strong></strong>`` with the text spilled outside.
- **Metadata.** Emitted at ``level`` NULL and ahead of the body, where it
  rendered as body prose.

**Metadata position follows the source** (duck_block spec v1.1)

.. list-table::
   :header-rows: 1
   :widths: 40 30 30

   * - Where it is in the source
     - Emitted
     - ``attributes['role']``
   * - Frontmatter before the body
     - first, ``element_order`` 0
     - ``'frontmatter'``
   * - Frontmatter after the body
     - appended
     - ``'tailmatter'``
   * - ``<title>``/``<meta>`` (in ``<head>``)
     - appended
     - *absent*

Frontmatter is ``kind = 'block'``, so filtering on ``kind = 'block'`` alone does
not exclude it. Use ``kind = 'block' AND element_type <> 'metadata'`` for body
content.

**Vocabulary**

- Vendored ``duck_block`` vocabulary at upstream ``b3b1e26``
  (``SPEC_VERSION`` 6.3), verified against upstream by name *and* value --
  a rename is a compile error, but a changed value would compile clean and
  silently stop matching. ``make check-vocabulary`` runs the check.
- ``duck_block_spec_version()`` reports 6.3; the public spec name is v1.1.
  Those axes are deliberately separate.

**Documentation**

- The reference documented ``block_type`` and ``block_order`` -- field names the
  struct has never had, so ``SELECT b.block_type`` copied from the docs failed
  with a binder error. Corrected, along with ``kind`` (previously undocumented),
  the content rule, structural lists, and metadata.
- ``make check-docs`` executes every SQL example against the built extension:
  105 examples, 99 running clean, 0 broken.

3828 assertions across 100 test cases.

v2.8.1
------

Adds first-class HTML block table functions and migrates JSON codecs to
``yyjson``.

**Changes**

- **First-class table functions.** Added ``read_html_blocks`` and
  ``parse_html_blocks`` table functions matching ``read_markdown_blocks`` and
  aligning with ``duck_block_utils``.
- **CSS and script isolation.** Filter out non-content tags (``<style>``,
  ``<script>``, ``<noscript>``, ``<template>``, ``<svg>``) from block text and
  inline elements while preserving frontmatter scripts.
- **yyjson migration.** High-performance JSON codecs replacing bespoke
  tokenizers.

v2.8.0
------

Native yyjson codecs for HTML tables and JSON-to-XML.

Replaced regex-based string extraction, bracket counting, and custom JSON
parsers (~990 lines) with DuckDB's bundled ``yyjson`` library.

**Highlights**

- **HTML ↔ table JSON conversion.** ``PandocTableToHtml``, ``TableJsonToHtml``,
  ``ListItemsToJson``, and ``TableToJson`` now use native ``yyjson_val``
  traversal and ``yyjson_mut_doc`` emission.
- **JSON-to-XML parsing.** ``XMLUtils::JSONToXML`` rewritten using
  ``yyjson_read`` and clean recursive XML DOM tree generation.
- **Full test pass.** All 94 test suites (3,155 assertions) passing cleanly.

v2.7.0
------

A correctness release covering five reported issues. The headline is that
``html_extract_*`` functions now accept plain ``VARCHAR`` without an explicit
``::HTML`` cast, and that a malformed XPath now raises instead of silently
returning an empty result. Shippable artifacts continue to build against the
**DuckDB v1.5.4** release tag.

Two of these change behavior in ways worth reading before upgrading: the XPath
change (Issue #134) and the ``html_extract_tables_json`` return type
(Issue #130).

**Behavior changes (review before upgrading)**

- **Malformed XPath now raises.** ``xmlXPathEvalExpression`` returns NULL for
  two unrelated reasons: the expression failed to **parse**, and the
  expression parsed but could not be **evaluated**. All 33 call sites guarded
  with ``if (xpath_obj)`` and fell through to an empty result, so a typo, an
  unclosed bracket, or a CSS selector passed by mistake was indistinguishable
  from a valid expression that legitimately matched nothing. Compiling before
  evaluating separates the two cases:

  .. list-table::
     :header-rows: 1

     * - expression
       - result
     * - ``//h2``
       - ``[Two]`` — unchanged
     * - ``//h2[``
       - now raises ``Invalid XPath expression: '//h2['``
     * - ``this is not xpath``
       - now raises
     * - ``h2``
       - ``[]`` — valid XPath, correctly matches nothing
     * - ``//gml:posList`` (undeclared prefix)
       - ``[]`` — unchanged

  A query that silently returned empty because the expression was malformed
  now raises ``Invalid Input Error``. That is the point of the change — an
  empty result meaning "your query is wrong" and one meaning "no matches" are
  different facts — but it will surface expressions that were quietly broken.
  An undeclared namespace prefix keeps returning empty: that case fails at
  evaluation, not at parse, and the distinction is pinned by tests so it
  cannot regress. Not addressed: ``count(//h2)`` still returns ``[]`` because
  the result is a number rather than a node-set. (Issue #134)
- **Fixed-shape return type.** ``html_extract_tables_json``'s
  content-dependent ``rows`` field is replaced by a fixed-shape
  ``table_json VARCHAR``, so the type no longer depends on the document.
  Callers destructure it with the JSON functions:

  .. code-block:: sql

     SELECT html_extract_tables_json(page)[1].table_json ->> '$.records[0].h';

  In practice nothing could have depended on the old shape — every access to
  it raised (see Bug Fixes below). (Issue #130)

**New Features**

- **VARCHAR accepted directly.** Every ``html_extract_*`` function previously
  required an explicit ``::HTML`` cast, so the obvious form failed:

  .. code-block:: sql

     SELECT html_extract_text(content, '//h2') FROM read_text('page.html');
     -- No function matches ... candidates: html_extract_text(HTML, VARCHAR)

  VARCHAR overloads are now registered alongside the ``HTML`` ones; existing
  ``HTML``-typed calls are unchanged. (Issue #129)
- **Footer rows are now modelled.** A footer row was either silently dropped
  by ``html_to_duck_blocks`` or silently relabelled as data by
  ``html_extract_table_rows``, neither distinguishable from a table with no
  footer. ``html_extract_table_rows`` now reports ``row_type = 'footer'``.
  (Issue #131)

**Bug Fixes**

- Fixed ``html_extract_tables_json`` being effectively unusable — two faults,
  both reachable from a two-line document: a table with no ``<th>`` raised
  ``INTERNAL Error: Value::LIST(values) cannot be used to make an empty list``
  (an INTERNAL error is an assertion failure and should not be reachable from
  input); and the declared return type never matched the value, since the
  ``rows`` field's element type was derived from the table's own header names
  but a scalar function's return type is fixed at bind time — any cast,
  ``len()``, or field access failed with ``Mismatch Type Error`` or
  ``Could not find key "metadata" in struct``. (Issue #130)
- Fixed invalid JSON for control characters — ``EscapeJsonString`` escaped
  only ``"``, ``\``, ``\n``, ``\r`` and ``\t``; every other control character
  was copied through verbatim, and JSON forbids unescaped bytes below
  ``0x20``. A table or list block built from HTML containing one — a vertical
  tab, say — carried content that was not valid JSON. All C0 control
  characters are now ``\uXXXX``-escaped. (Issue #132)

**CI**

- The DuckDB-``main`` canary shared a job name with the shippable build, so an
  expected upstream breakage read as a failing required check on every pull
  request. It now runs only on ``main`` and on manual dispatch, and is named
  as advisory.
- The format check, which had been commented out entirely, is re-enabled.

v2.6.1
------

Fixes to ``html_to_duck_blocks`` structured output.

**Bug Fixes**

- **Nested inline formatting preserved.** ``<b>x <i>y</i></b>`` previously
  flattened to one ``bold`` inline (dropping the inner ``<i>``); now nested
  formatting (bold links, emphasis-in-strong, code-in-link) is emitted as
  structured ``kind='inline'`` children — a wrapper with nested elements
  becomes a container followed by its children at ``level+1``.
- **NULL container content**, matching the ``duckdb_markdown`` /
  ``duck_block_utils`` spec convention.
- **Aligned level convention.** Top-level blocks are depth 1, direct inlines
  are level 2, and nested elements are level+1; blockquote keeps its own
  nesting depth.

**Testing**

- Verified end-to-end with ``duck_block_utils``' renderer. Full suite green
  (3104 assertions).

v2.6.0
------

A multi-file release. ``read_xml`` / ``read_html`` now process a glob or list of files
**across threads** (order-preserving), and schema inference samples **multiple files** by
default instead of only the first — so a column or wider type that appears only in a later
file is no longer missed. Shippable artifacts still build against the **DuckDB v1.5.4**
release tag.

**Behavior changes (review before upgrading)**

- **Multi-file schema inference samples several files by default.** ``read_xml`` / ``read_html``
  over a glob or list previously inferred the schema from the *first file only*; they now
  sample up to ``sample_files`` files (**default 8**) and merge the result, using the same
  machinery as ``union_by_name``. Consequences: the inferred schema may now include columns /
  wider types that only appear in files 2–8; and reading ``[invalid, valid]`` with
  ``ignore_errors`` now recovers a real schema from the valid file rather than falling back to
  the raw ``xml`` / ``html`` column. ``sample_files := 1`` restores the exact first-file-only
  behavior; ``sample_files := -1`` samples every file. For file sets at or below the sample
  bound, the default now matches ``union_by_name`` — the latter remains the unbounded
  "scan every file" form (it still differs for globs larger than the bound). (Issue #124)
- **Glob matches are sorted lexicographically.** A glob (e.g. ``read_xml('*.xml')``) now yields
  files in deterministic, filesystem-independent order. This is the order preserved across
  threads *and* the order in which the first ``sample_files`` files are chosen for inference. An
  explicit list of paths keeps the order you provide. (Issue #72)

**New Features**

- **Multi-file parallelism.** ``read_xml`` / ``read_xml_objects`` / ``read_html`` /
  ``read_html_objects`` process a glob or list of files across threads — one worker per file —
  instead of single-threaded (``MaxThreads()`` was hardcoded to 1). Output stays in file order
  regardless of thread count via DuckDB's batch-index reassembly. Per-file cursor state moved
  from the global state into a new per-thread ``XMLReadLocalState``; single-file reads are
  unchanged (one file → one worker → the existing serial path, incl. the 2–4 GiB whole-document
  path). (Issue #72)
- **Bounded multi-file schema sampling** via the new ``sample_files`` parameter (default 8),
  making inference robust to columns/types beyond the first file without opening an entire glob.
  (Issue #124)

**Internal**

- ``vcpkg.json`` manifest version → 2.6.0.
- New tests: ``test/sql/xml_multifile_parallelism.test`` (forced-parallel order/parity/filename
  attribution) and ``test/sql/xml_multifile_schema_inference.test`` (multi-file column/type
  pickup; written test-first to pin the prior bug). Three existing tests updated to assert the
  improved default and use ``sample_files := 1`` as the narrow baseline.

v2.5.0
------

A security-hardening release. XML/HTML parsing is now XXE-safe and non-networked
by default; the >2 GiB whole-document fix is completed for the remaining HTML
parse sites; libxml2 error handling is made thread-safe by construction; and
Windows (MSVC) CI is re-enabled. Shippable artifacts continue to build against
the **DuckDB v1.5.4** release tag; the submodules now track the ``v1.5-variegata``
branch for development. All platforms green (89 test cases / 2925 assertions),
including the re-enabled ``windows_amd64`` MSVC job.

**Behavior changes (review before upgrading)**

- **External entity resolution is disabled by default (XXE-safe).** A document
  containing an external ``SYSTEM``/``PUBLIC`` entity — e.g.
  ``<!ENTITY x SYSTEM "file:///etc/passwd">`` or an ``http://`` URL — no longer
  fetches the referenced file/URL; the reference resolves to nothing. Parsing also
  runs with ``XML_PARSE_NONET`` / ``HTML_PARSE_NONET`` so no network access is
  attempted on a document's behalf. This closes an XXE/SSRF exposure and is a
  semantic change for the (unsafe) case of relying on external-entity inlining;
  internal entities within libxml2's DoS limits are unaffected. (Issue #115, PR #118)

**Security / hardening**

- Fail-closed external-entity loader installed at every parse site
  (``EnsureSecureParsing()``), composed with ``*_NONET``. (PR #118)
- New adversarial/security test coverage: an XXE canary (referenced file must never
  appear in extracted text), bounded entity expansion (billion-laughs stays capped),
  deep-nesting rejection, and truncated/bad-encoding/unclosed inputs rejected cleanly
  (``test/sql/adversarial_xml.test``, ``test/sql/xxe_external_entity.test``). (PR #117, #118)
- libxml2 error handling no longer mutates shared global state: the removed
  ``xmlSetStructuredErrorFunc`` global is replaced by per-context handlers
  (``xmlXPathSetErrorHandler``), eliminating a cross-thread race in the in-process
  multi-threaded scalar paths. (PR #119, follow-up to Issue #7)

**Bug Fixes**

- **>2 GiB HTML documents.** The remaining whole-value HTML parse sites
  (``html_to_duck_blocks``, ``HTMLUnescape``) used ``htmlReadMemory``, whose ``int``
  length parameter overflowed above INT_MAX (~2.147 GiB). Both now parse via the
  IO-based ``htmlReadIO`` through a shared ``src/include/xml_in_memory_reader.hpp``
  reader, completing the large-document fix started for XML in v2.4.0 (#103/#112).
  The ceiling remains DuckDB's 4 GiB single-value cap. (Issue #115, PR #117)

**Build / dependencies**

- Submodules now track the ``v1.5-variegata`` branch (``duckdb`` → ``b155d6f``,
  ``extension-ci-tools`` → ``72e76e9``). The ``duckdb-stable-build`` distribution job
  keeps ``duckdb_version`` / ``ci_tools_version`` = **v1.5.4** (the release tag), because
  branch-tip ``-dev`` builds produce non-loadable extensions. ``duckdb-next-build``
  (DuckDB ``main``) is unchanged. (Issue #107, PR #121)
- **Windows re-enabled.** ``windows_amd64`` (MSVC) is built and tested again in both
  build jobs; ``windows_amd64_mingw`` stays excluded (the cp1252 reporter crash is
  mingw-specific). (PR #121)

**Docs**

- README gains a "Security & Trust Model" section documenting the XXE-safe posture,
  entity handling, the libxml2 DoS limits, and the 4 GiB single-value cap. (PR #117)
- Refreshed stale test statistics (89 suites / 2900+ assertions) and DuckDB version
  references (v1.5+) across README and the Sphinx docs.

**Internal**

- ``vcpkg.json`` manifest version → 2.5.0.
- GitHub-issue test coverage: NULL-safe SAX/DOM list equivalence
  (``IS NOT DISTINCT FROM``) so nested-type ``NULL = NULL`` semantics don't produce a
  false canary failure against DuckDB ``main``. (Issue #77, PR #120)

v2.4.0
------

A feature-and-robustness release. ``read_xml`` type detection now catches
out-of-sample outliers by default (the pragmatic part of issue #102 "C"); the XML
serializers emit consistent literal-UTF-8 / DOM-parity output; whole-document reads
work past the old 2 GiB barrier (up to DuckDB's 4 GiB single-value cap); and the
stable build moves to **DuckDB v1.5.4** (Issue #107).

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

**New Features**

- **Large XML/HTML documents (2 GiB – 4 GiB).** ``read_xml`` / ``read_xml_objects`` /
  ``read_html`` previously failed on documents larger than ~2 GiB (e.g. the ~2.8 GiB Czech
  justice bulk register) with ``Invalid argument`` (macOS) or ``contains invalid XML``
  (Linux), even with ``maximum_file_size`` raised above the file size. Two independent
  ~2 GiB barriers are fixed: the whole-file read is now chunked (``ReadFileFully``, ≤1 GiB
  per call, advancing by the bytes actually returned), and ``XMLDocRAII`` parses via the
  IO-based ``xmlCtxtReadIO`` / ``htmlReadIO`` instead of ``xmlCtxtReadMemory`` /
  ``htmlReadMemory`` (whose ``int`` length parameter overflowed above INT_MAX). No
  ``XML_PARSE_HUGE`` — the libxml2 DoS limits from the v2.2.0 hardening are preserved. The
  new ceiling is DuckDB's 4 GiB single-value cap; larger inputs still need record-level
  streaming via ``read_xml``. (Issue #103, PR #112; read fix based on @onnimonni's #103)

**Bug Fixes — serialization consistency**

- ``xml_extract_elements(...)`` / ``XMLFragment::VARCHAR`` now serialize non-ASCII text as
  literal UTF-8 instead of escaping every non-ASCII character to a numeric character
  reference (``ö`` → ``&#xF6;``). The encoding-less fragment serializer is switched to
  ``xmlDocDumpFormatMemoryEnc(..., "UTF-8", 0)``, matching the ``read_xml`` reader capture
  and ``xml_extract_text``. (Issue #108, PR #110 by @marcel-more)
- ``to_xml`` / ``xml`` (and the ``LIST`` / ``STRUCT`` forms) likewise now keep non-ASCII text
  literal rather than NCR-escaping it. As a result the XML declaration these emit now states
  the encoding explicitly — ``<?xml version="1.0" encoding="UTF-8"?>`` instead of
  ``<?xml version="1.0"?>`` — so consumers that exact-string-match the declaration should
  update. (Follow-up to #108 / #110)
- SAX streaming now serializes control characters in captured raw XML the same way the DOM
  path does: a carriage return becomes ``&#13;`` in text content, and CR/LF/TAB become
  ``&#13;`` / ``&#10;`` / ``&#9;`` in attribute values (XML 1.0 §2.11 / §3.3.3). Previously
  the SAX path emitted a raw CR byte, so a captured-subtree ``VARCHAR`` was not byte-identical
  across reader modes and could silently break downstream ``\s``-based text processing.
  (Issue #109, PR #111 by @marcel-more)

**Known limitations / next**

- This is the *large-default* form of #102 "C", not unconditional runtime widening: an
  outlier past an explicitly-set, too-small ``sample_size`` still errors (or NULLs under
  ``ignore_errors``). True runtime VARCHAR widening remains the tracked follow-up — the
  ``XmlUncastableValue`` chokepoint is in place (see
  ``test/sql/issue_102_runtime_widening.test.future``).

**Build / dependencies**

- Stable build moved to **DuckDB v1.5.4**: ``duckdb`` submodule → ``08e34c4`` and
  ``extension-ci-tools`` → ``b777c70`` (v1.5.4), with the ``duckdb-stable-build`` CI pins
  (``duckdb_version`` / ``ci_tools_version`` / the reusable workflow ref) and the WASM
  artifact names bumped to match. Resolves the missing v1.5.4 community-extensions build.
  The ``duckdb-next-build`` (DuckDB ``main``) job is unchanged. (Issue #107)

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
