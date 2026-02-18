# webbed v1.4.0 Release Notes

## Overview

This release introduces new `parse_xml` and `parse_html` functions for parsing XML/HTML content directly from strings, complementing the existing file-based `read_xml` and `read_html` functions. Also includes a bug fix for CDATA section handling.

## New Features

### String-based XML/HTML Parsing

New table functions for parsing XML/HTML content from strings instead of files:

- **`parse_xml_objects(xml_string)`** - Parse XML string and return raw content as XMLType
- **`parse_html_objects(html_string)`** - Parse HTML string and return raw content as HTMLType
- **`parse_xml(xml_string, [options])`** - Parse XML string with schema inference
- **`parse_html(html_string, [options])`** - Parse HTML string with schema inference

**Basic Usage:**
```sql
-- Parse XML string to raw content
SELECT * FROM parse_xml_objects('<root><item>test</item></root>');

-- Parse XML with schema inference
SELECT title, price
FROM parse_xml('<catalog><book><title>DuckDB</title><price>29.99</price></book></catalog>');

-- Parse with explicit schema
SELECT * FROM parse_xml('<root><item>42</item></root>', columns := {item: 'INTEGER'});

-- Error handling
SELECT * FROM parse_xml_objects('invalid xml', ignore_errors := true);
```

**Supported Parameters:**

All `parse_*` functions support:
- `ignore_errors` (BOOLEAN) - Return empty result instead of failing on invalid input

`parse_xml` and `parse_html` support all schema inference parameters from their `read_*` counterparts:
- `root_element`, `record_element`, `force_list`
- `attr_mode`, `attr_prefix`, `text_key`
- `namespaces`, `empty_elements`
- `auto_detect`, `max_depth`
- `unnest_as`, `all_varchar`, `columns`

## Bug Fixes

- **Fixed CDATA sections converted to empty objects in xml_to_json (#63)** - CDATA content is now properly preserved when converting XML to JSON

## Testing

- Added comprehensive test suite for parse_xml and parse_html functions
- 17 new test assertions for string-based parsing
