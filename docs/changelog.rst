Changelog
=========

v2.3.0 (Current)
-----------------

**Bug Fixes**

- Fixed the DuckDB-WASM build installing but failing to load — libxml2 (and its
  ``zlib`` dependency) were not linked into the ``emcc -sSIDE_MODULE=2`` side
  module, so their symbols were left unresolved and the ``.wasm`` failed to load.
  ``target_link_libraries`` is ignored by that separate emcc link step; both
  archives are now passed via ``LINKED_LIBS``. (Issue #96)
- Fixed SAX streaming dropping the attributes of a record's repeated nested child
  elements — a ``LIST<STRUCT(@attr…)>`` streamed back as ``[NULL, NULL]`` while the
  DOM path returned the attribute values. Each direct child's own attributes are
  now carried through the fragment extractor, restoring DOM/SAX parity. (Issues #97, #98)

**Testing**

- Added a ``test/wasm/`` regression suite wired into CI as the ``wasm-load-test``
  job: a runtime-free static symbol check (hard gate — libxml2/zlib must be linked
  into the side module, not left unresolved) plus a duckdb-wasm live load test
  (informational / ``continue-on-error`` until official duckdb-wasm ships v1.5.3).
  The reusable distribution workflow builds the ``.wasm`` but never loads it, which
  is why #96 shipped green.
- Added ``test/sql/sax_nested_child_attr_parity.test`` (DOM/SAX parity for the
  repeated-nested-attribute case); native suite at 83 cases / 2875 assertions.

**Internal**

- ``vcpkg.json`` manifest version → 2.3.0. No DuckDB submodule bump (stays on v1.5.3).

v2.0.0
-----------------

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
