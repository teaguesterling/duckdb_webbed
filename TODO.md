# TODO List - Known Issues and Limitations

This file tracks known issues where tests have been updated to reflect current (incorrect) behavior rather than expected behavior. Each item should eventually be fixed and the corresponding tests updated.

## High Priority - Feature Gaps

(None currently)

## Medium Priority - Error Handling

(None currently)

## Intentional Behavior (Consistent with read_json)

### Empty File List Throws Error
**Files**: `test/sql/html_validation.test:19`
**Behavior**: `read_html(CAST([] AS VARCHAR[]))` throws "No files found" error
**Rationale**: Consistent with `read_json` behavior - empty file list is likely a user error

### ignore_errors Doesn't Prevent "No Files Found" Error
**Files**: `test/sql/html_schema_errors.test:23`
**Behavior**: `ignore_errors:=true` still throws error for non-existent files
**Rationale**: Consistent with `read_json` - `ignore_errors` handles parsing errors, not missing files

## Low Priority - Input Validation

(None currently)

## Fixed - No Action Needed

### Cross-record Schema Inference for Repeated Elements (Issue #35)
**Status**: ✅ FIXED
**Files**: Multiple test files updated
**Description**: Schema inference now correctly examines ALL records to detect repeated elements

### Opaque Type Parameterization (Issue #18)
**Status**: ✅ FIXED
**Files**: Multiple test files updated
**Description**: HTML files now correctly show "HTML" type instead of "XML" for opaque elements

### Custom HTML Elements Not Recognized (Issue #51)
**Status**: ✅ FIXED
**Description**: HTML parser now correctly recognizes custom elements like `<data-item>`, `<data-records>`
**Fix**: Use HTML parser based on `opaque_type_name` in schema inference and extraction

### HTML union_by_name Returns NULL Data (Issue #48 partial)
**Status**: ✅ FIXED
**Description**: `union_by_name` now correctly extracts data from multiple HTML files
**Fix**: Fixed LIST extraction and record element serialization in ExtractDataWithSchema
**Remaining**: filename parameter tracking still needs work

### Cross-record Attribute Discovery for Nested Elements (Issue #50)
**Status**: ✅ FIXED
**Description**: Nested elements with attributes discovered in later records now create proper STRUCT types
**Fix**: Added cross-record attribute collection in `InferColumnType` and `#text` handling in `ExtractStructFromNode`
**Schema**: Elements like `<phone type="mobile">555-1234</phone>` become `STRUCT("#text" VARCHAR, "type" VARCHAR)`

### Invalid XPath Now Returns Error
**Status**: ✅ FIXED
**Description**: Invalid XPath expressions in `record_element` now throw clear errors instead of silently returning 0 rows
**Fix**: Added XPath validation in `IdentifyRecordElements` with clear error messages

### Parameter Validation for attr_mode, namespaces, empty_elements
**Status**: ✅ FIXED
**Description**: Invalid parameter values now throw clear errors with valid options listed
**Tests**: `test/sql/html_validation.test:31-47`

### max_depth Validation and idx_t Type
**Status**: ✅ FIXED
**Description**: `max_depth` now uses `idx_t` (consistent with DuckDB JSON extension), `-1` means unlimited, other negative values throw an error
**Tests**: `test/sql/html_validation.test:7-15`

### Schema Inference for Heterogeneous Repeated Elements (Issue #47)
**Status**: ✅ FIXED
**Description**: Repeated elements with completely different child schemas now correctly merge all children
**Example**: `<section>` with `address` child and `<section>` with `skill-item` children now both appear in schema
**Fix**: Modified `InferColumnType` to iterate ALL instances when discovering child elements, not just the first instance
**Tests**: `test/sql/html_complex_types.test`

## Summary

**Total Issues**: 0 items requiring fixes
- High Priority: 0 items
- Medium Priority: 0 items
- Low Priority: 0 items

**GitHub Issues Created**:
- #46: Type inference for semantic HTML elements with attributes
- #47: Schema inference fails for heterogeneous repeated elements (FIXED)
- #48: HTML union_by_name returns NULL data with multiple files (partially fixed)
- #49: Enable type inference for elements with attributes
- #50: Cross-record attribute discovery and mixed-content handling (FIXED)
- #51: Custom HTML elements not recognized by HTML parser (FIXED)
