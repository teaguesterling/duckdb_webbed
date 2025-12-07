# TODO List - Known Issues and Limitations

This file tracks known issues where tests have been updated to reflect current (incorrect) behavior rather than expected behavior. Each item should eventually be fixed and the corresponding tests updated.

## High Priority - Feature Gaps

(None currently)

## Medium Priority - Error Handling

### Empty File List Should Return Empty Result
**Files**: `test/sql/html_validation.test:19`
**Current Behavior**: `read_html(CAST([] AS VARCHAR[]))` throws "No files found" error
**Expected Behavior**: Should return empty result set (0 rows)
**Impact**: Makes it harder to handle dynamic file lists

### ignore_errors Doesn't Prevent "No Files Found" Error
**Files**: `test/sql/html_schema_errors.test:23`
**Current Behavior**: `ignore_errors:=true` still throws error for non-existent files
**Expected Behavior**: Should handle gracefully and return empty result or skip missing files
**Impact**: Less robust error handling for batch processing

### Invalid XPath Returns 0 Rows Instead of Error
**Files**: `test/sql/html_schema_errors.test:37`
**Current Behavior**: Invalid XPath like `'[[[invalid xpath'` returns 0 rows silently
**Expected Behavior**: Should throw clear error message about invalid XPath syntax
**Impact**: Silent failures make debugging difficult

## Low Priority - Input Validation

### Parameter Validation Not Implemented
**Files**: `test/sql/html_validation.test:29, 35, 41`
**Current Behavior**: Invalid values for `attr_mode`, `namespaces`, `empty_elements` are silently ignored
**Expected Behavior**: Should validate and throw errors for invalid parameter values
**Impact**: Typos in parameters go unnoticed, leading to unexpected behavior

### Negative max_depth Validation
**Files**: `test/sql/html_validation.test:10`
**Current Behavior**: `max_depth:=-1` is allowed (treated as very large depth)
**Expected Behavior**: TBD - either validate and reject negative values, or document that -1 means unlimited
**Impact**: Unclear API behavior

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

## Summary

**Total Issues**: 6 items requiring fixes
- High Priority: 0 items
- Medium Priority: 3 items (error handling)
- Low Priority: 3 items (input validation)

**GitHub Issues Created**:
- #46: Type inference for semantic HTML elements with attributes
- #47: Schema inference fails for heterogeneous repeated elements
- #48: HTML union_by_name returns NULL data with multiple files (partially fixed)
- #49: Enable type inference for elements with attributes
- #50: Cross-record attribute discovery and mixed-content handling (FIXED)
- #51: Custom HTML elements not recognized by HTML parser (FIXED)
