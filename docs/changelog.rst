Changelog
=========

v1.3.1 (Current)
----------------

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
