# read_html and read_xml Feature Parity Analysis

**Date**: 2025-11-27
**Issue**: #18
**Branch**: issue-18-html-xml-feature-parity

## Executive Summary

Analysis shows that `read_html` and `read_xml` have **near-complete parameter parity** at the API level. All 16 core parameters are present in both functions. However, there is a **significant test coverage gap**: XML has 26 test files while HTML has only 5.

## Parameter Comparison

### Parameters Present in Both Functions ✓

Both `read_html` and `read_xml` support these parameters:

1. **File Processing**:
   - `ignore_errors` (BOOLEAN) - Skip invalid files
   - `maximum_file_size` (BIGINT) - File size limit
   - `union_by_name` (BOOLEAN) - Merge schemas by column name

2. **Schema Inference**:
   - `root_element` (VARCHAR) - Starting element for parsing
   - `record_element` (VARCHAR) - XPath/tag for row elements
   - `attr_mode` (VARCHAR) - Attribute handling: 'columns', 'prefixed', 'map', 'discard'
   - `attr_prefix` (VARCHAR) - Prefix when attr_mode='prefixed'
   - `text_key` (VARCHAR) - Key for mixed text content
   - `namespaces` (VARCHAR) - Namespace handling: 'strip', 'expand', 'keep'
   - `empty_elements` (VARCHAR) - Empty element handling: 'null', 'string', 'object'
   - `auto_detect` (BOOLEAN) - Enable automatic schema detection
   - `max_depth` (INTEGER) - Maximum nesting depth
   - `unnest_as` (VARCHAR) - How to unnest: 'columns' or 'struct'

3. **Type Control**:
   - `force_list` (VARCHAR or LIST(VARCHAR)) - Elements always as LIST
   - `columns` (ANY) - Explicit schema specification
   - `all_varchar` (BOOLEAN) - Force all scalars to VARCHAR

### Parameters Unique to read_html

- `filename` (BOOLEAN) - Include filename in output

**Assessment**: HTML has this extra parameter likely because HTML files are often processed in batches where tracking the source file is important.

### Parameters Unique to read_xml

None. XML has no parameters that HTML lacks.

## Test Coverage Comparison

### XML Test Files (26 total)

1. ✓ xml_all_varchar.test
2. ✓ xml_array_support.test
3. ✓ xml_basic.test
4. ✓ xml_complex_types.test
5. ✓ xml_deep_hierarchies.test
6. ✓ xml_document_analysis.test
7. ✓ xml_document_formatting.test
8. ✓ xml_enhanced_simple.test
9. ✓ xml_enhanced_to_xml.test
10. ✓ xml_force_list.test
11. ✓ xml_function_fixes.test
12. ✓ xml_hybrid_schemas.test
13. ✓ xml_json_conversion.test
14. ✓ xml_large_files.test
15. ✓ xml_large_row_count.test
16. ✓ xml_max_depth.test
17. ✓ xml_replacement_scan.test
18. ✓ xml_rss_feed.test
19. ✓ xml_schema_errors.test
20. ✓ xml_schema_validation.test
21. ✓ xml_table_functions.test
22. ✓ xml_type_casting.test
23. ✓ xml_type_inference_order.test
24. ✓ xml_union_by_name.test
25. ✓ xml_validation.test
26. ✓ xml_xpath_extraction.test

### HTML Test Files (5 total)

1. ✓ html_basic.test
2. ✓ html_basic_functions.test
3. ✓ html_entity_encoding.test
4. ✓ html_extraction.test
5. ✓ html_file_reading.test
6. html_schema_inference.test.future (disabled)

### Missing HTML Test Coverage

Features tested for XML but NOT for HTML:

1. **all_varchar** - Force scalar types to VARCHAR
2. **array_support** - Array/list handling
3. **complex_types** - Complex nested structures
4. **deep_hierarchies** - Deep nesting scenarios
5. **document_analysis** - Document structure analysis
6. **document_formatting** - Output formatting
7. **enhanced_simple** - Enhanced simple queries
8. **enhanced_to_xml** - Conversion back to XML
9. **force_list** - Force elements to LIST type
10. **function_fixes** - Function-specific fixes
11. **hybrid_schemas** - Mixed schema handling
12. **json_conversion** - XML to JSON conversion
13. **large_files** - Large file handling
14. **large_row_count** - Many rows handling
15. **max_depth** - Max depth parameter
16. **replacement_scan** - Replacement scan functionality
17. **rss_feed** - RSS feed parsing (domain-specific to XML)
18. **schema_errors** - Error handling in schema inference
19. **schema_validation** - Schema validation
20. **table_functions** - Table function variations
21. **type_casting** - Type casting functionality
22. **type_inference_order** - Type inference priority
23. **union_by_name** - Union by name functionality
24. **validation** - General validation
25. **xpath_extraction** - XPath queries

## Recommendations

### Priority 1: High-Value Tests (should apply to HTML)

These features are fundamental and should work identically in HTML:

- [ ] `all_varchar` - Test parameter works
- [ ] `array_support` - List/array handling
- [ ] `complex_types` - Nested structures
- [ ] `deep_hierarchies` - Deep nesting
- [ ] `force_list` - Force list parameter
- [ ] `large_files` - Large file support
- [ ] `max_depth` - Max depth parameter
- [ ] `schema_errors` - Error handling
- [ ] `type_casting` - Type conversions
- [ ] `type_inference_order` - Correct type priority
- [ ] `union_by_name` - Schema merging

### Priority 2: Medium-Value Tests (likely applicable)

- [ ] `document_analysis` - Structure analysis
- [ ] `hybrid_schemas` - Mixed schemas
- [ ] `large_row_count` - Many rows
- [ ] `replacement_scan` - Replacement scan
- [ ] `schema_validation` - Validation
- [ ] `table_functions` - Function variations
- [ ] `validation` - General validation

### Priority 3: XML-Specific (may not apply to HTML)

These are specific to XML format and may not be relevant:

- ~~rss_feed~~ - RSS is XML-specific
- ~~json_conversion~~ - XML⟷JSON specific
- ~~enhanced_to_xml~~ - Conversion to XML
- ~~xpath_extraction~~ - XPath is XML-specific (though HTML supports XPath)
- ~~document_formatting~~ - XML-specific formatting

## Implementation Plan

### Phase 1: Verify Parameter Functionality

Create basic tests to verify all shared parameters work correctly in `read_html`:

1. Test `all_varchar` parameter
2. Test `force_list` parameter
3. Test `max_depth` parameter
4. Test `union_by_name` with multiple HTML files
5. Test `columns` explicit schema
6. Test error handling with `ignore_errors`

### Phase 2: Add Critical Test Coverage

Port the most important XML tests to HTML equivalents:

1. `html_all_varchar.test` - Based on xml_all_varchar.test
2. `html_array_support.test` - List/array handling
3. `html_complex_types.test` - Nested structures
4. `html_force_list.test` - Force list parameter
5. `html_type_inference.test` - Type detection priority
6. `html_union_by_name.test` - Schema merging
7. `html_schema_errors.test` - Error handling

### Phase 3: Comprehensive Coverage

Add remaining applicable tests:

1. `html_deep_hierarchies.test`
2. `html_max_depth.test`
3. `html_large_files.test`
4. `html_type_casting.test`
5. `html_validation.test`

## Current Status

- [x] Parameter audit completed
- [x] Test coverage analysis completed
- [x] Create Priority 1 tests (5 test suites, 34 test cases)
- [x] Create Priority 2 tests (4 test suites, 28 test cases)
- [ ] Document any HTML-specific limitations
- [ ] Update README with HTML feature documentation

### Test Suites Added

**Priority 1 (Critical Features):**
1. ✅ html_all_varchar.test - 7 tests
2. ✅ html_force_list.test - 7 tests
3. ✅ html_union_by_name.test - 6 tests
4. ✅ html_type_inference.test - 7 tests
5. ✅ html_max_depth.test - 7 tests

**Priority 2 (Important Features):**
6. ✅ html_complex_types.test - 7 tests
7. ✅ html_schema_errors.test - 7 tests
8. ✅ html_validation.test - 8 tests
9. ✅ html_large_files.test - 7 tests

**Total New Coverage:** 9 test suites, 62 test cases

**HTML Test Files:** 5 → 14 files (180% increase)
**Test Case Count:** ~25 → ~87 cases (248% increase)

## Notes

- HTML parsing uses libxml2's HTML parser, which is more lenient than the XML parser
- HTML may have different nesting structures (e.g., implicit tags like `<tbody>`)
- Some HTML documents may not have well-defined "records" like XML documents
- XPath works on HTML when parsed as XML tree structure
