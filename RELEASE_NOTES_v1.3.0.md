# webbed v1.3.0 Release Notes

## Overview

This release introduces the duck_blocks document processing system, significantly improved XML namespace handling, and several bug fixes. The duck_blocks API is designed to integrate seamlessly with the `duck_block_utils` extension for full Markdown support.

## New Features

### Duck Blocks Document Processing

New functions for converting HTML documents to and from structured block representations:

- **`html_to_duck_blocks(html)`** - Parse HTML into a list of structured duck_block elements
- **`duck_blocks_to_html(blocks)`** - Convert duck_block elements back to HTML

The duck_block structure provides a standardized representation:
```sql
STRUCT(kind, element_type, content, level, encoding, attributes, element_order)
```

Supported block types include: `paragraph`, `heading`, `code_block`, `blockquote`, `list_item`, `horizontal_rule`, `table`, `image`, `metadata`, and inline elements (`text`, `bold`, `italic`, `code`, `link`, `image`, `linebreak`, etc.).

**Features:**
- Round-trip HTML preservation (HTML -> duck_blocks -> HTML)
- Inline element support with structured parsing (#59)
- Table rendering with proper `<thead>`/`<tbody>` structure (#57)
- Frontmatter preservation using `<script type="application/vnd.frontmatter+yaml">` tags (#56)

### XML Namespace Improvements (#60)

New namespace modes for XPath functions:
- **`'auto'`** (recommended) - Automatically detects undeclared prefixes and either looks up common URIs or creates mock URIs
- **`'strict'`** - Requires all namespaces to be explicitly declared
- **`'ignore'`** - Ignores namespace declarations

New helper functions:
- **`xml_find_undefined_prefixes(xml, xpath)`** - Find undeclared namespace prefixes in XPath
- **`xml_add_namespace_declarations(xml, map)`** - Inject namespace declarations into XML
- **`xml_lookup_namespace(prefix)`** - Look up common namespace URIs (gml, svg, xlink, dc, etc.)

Updated functions to support namespace configuration:
- `xml_extract_text`
- `xml_extract_elements`
- `xml_extract_elements_string`
- `xml_extract_attributes`

### Implicit Type Casting

XML and HTML types now implicitly cast to VARCHAR (cost 1), allowing string functions to work on XML/HTML values while preferring direct XML/HTML function overloads.

## Breaking Changes

### Duck Blocks API Rename

Functions and types have been renamed from `doc_block` to `duck_block` for consistency with `duck_block_utils`:
- `html_to_doc_blocks` -> `html_to_duck_blocks`
- `doc_blocks_to_html` -> `duck_blocks_to_html`

### Duck Block Struct Changes

The struct format has been updated to align with `duck_block_utils`:
- Added `kind` field as first field (`'block'` or `'inline'`)
- Renamed `block_type` to `element_type`
- Renamed `block_order` to `element_order`
- Heading level (1-6) moved from `level` field to `attributes['heading_level']`
- `level` field now reserved for hierarchy depth (e.g., blockquote nesting)

### HTML Namespace Parameter Removed

The `namespace` parameter has been removed from `html_extract_text`. HTML5 parsing doesn't support XML namespace declarations (prefixed elements are treated as literal names with colons).

## Bug Fixes

- Fixed namespace mode handling in XPath functions (#60)
- Fixed HTML union_by_name bug (#48)

## Documentation

- Added comprehensive documentation for duck_block functions
- Added namespace mode documentation with `'auto'` mode recommendation
- Added integration examples with `duck_block_utils` extension
- Updated quick reference tables

## Testing

- 54 new assertions for duck_block API
- 37 new assertions for table round-trips
- Comprehensive namespace mode tests
