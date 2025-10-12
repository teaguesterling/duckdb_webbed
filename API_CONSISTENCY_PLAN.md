# API Consistency Refactoring Plan
**Date:** 2025-10-12
**Branch:** api-consistency-check
**Status:** Planning Phase

## Executive Summary

This plan addresses API inconsistencies between `read_xml` and `read_html` table functions. The goal is to unify their implementations, eliminate code duplication, and provide consistent schema inference capabilities for both XML and HTML documents.

## Problem Statement

### 1. Current API Inconsistencies

| Function | Purpose | Has `filename` param? | Has Schema Inference? | Implementation |
|----------|---------|----------------------|----------------------|----------------|
| `read_xml_objects` | Raw XML access | ✅ Yes | ❌ No | Unique |
| `read_xml` | Structured XML | ❌ **MISSING** | ✅ Yes | Unique |
| `read_html_objects` | Raw HTML access | ✅ Yes | ❌ No | **DUPLICATE** |
| `read_html` | Structured HTML? | ✅ Yes | ❌ **MISSING** | **DUPLICATE** |

**Location References:**
- xml_reader_functions.cpp:16-163 - `read_xml_objects` implementation
- xml_reader_functions.cpp:165-432 - `read_xml` implementation
- xml_reader_functions.cpp:561-630 - `ReadHTMLBind` (shared by both HTML functions)
- xml_reader_functions.cpp:514-553 - Registration shows `read_html` and `read_html_objects` are identical

### 2. Code Duplication

**Critical Finding:** `read_html` and `read_html_objects` call the SAME three functions:
- `ReadHTMLBind`
- `ReadHTMLFunction`
- `ReadHTMLInit`

This violates the pattern established by XML functions where:
- `*_objects` = raw content
- Base function = schema inference + structured data

### 3. XML vs HTML Parsing

**Key Infrastructure (xml_utils.cpp:65-92):**

The `XMLDocRAII` class already handles both parsing modes:

```cpp
XMLDocRAII::XMLDocRAII(const std::string &content, bool is_html) {
    if (is_html) {
        // Lenient HTML parser
        doc = htmlReadMemory(content.c_str(), content.length(), nullptr, nullptr,
                             HTML_PARSE_RECOVER | HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING);
    } else {
        // Strict XML parser
        xmlParserCtxtPtr parser_ctx = xmlNewParserCtxt();
        doc = xmlCtxtReadMemory(parser_ctx, content.c_str(), content.length(),
                                nullptr, nullptr, 0);
    }
}
```

**After parsing, both produce `xmlDoc` structures that support:**
- XPath queries
- Schema inference
- Data extraction
- All existing XML utility functions

**The only difference:** HTML parser is lenient and auto-closes tags.

## Proposed Solution

### Architecture: Unified Internal Functions

```cpp
// Add to XMLReadFunctionData structure
enum class ParseMode {
    XML,    // Strict XML parsing (xmlCtxtReadMemory)
    HTML    // Lenient HTML parsing (htmlReadMemory)
};

struct XMLReadFunctionData : public TableFunctionData {
    vector<string> files;
    bool ignore_errors = false;
    idx_t max_file_size = 16777216;
    ParseMode parse_mode = ParseMode::XML;  // NEW: Controls parser selection

    // For _objects functions
    bool include_filename = false;

    // For structured read_xml/read_html
    bool has_explicit_schema = false;
    vector<string> column_names;
    vector<LogicalType> column_types;

    // Schema inference options (shared by both XML and HTML)
    XMLSchemaOptions schema_options;
};
```

### Refactored API Surface

| Function | Parameters | Returns | Behavior |
|----------|-----------|---------|----------|
| `read_xml_objects` | file_pattern, ignore_errors, maximum_file_size, **filename** | [filename], xml | Raw XML content |
| `read_xml` | file_pattern, ignore_errors, maximum_file_size, **filename**, root_element, record_element, include_attributes, auto_detect, max_depth, unnest_as, columns | Inferred columns | Schema inference → structured data |
| `read_html_objects` | file_pattern, ignore_errors, maximum_file_size, **filename** | [filename], html | Raw HTML content |
| `read_html` | file_pattern, ignore_errors, maximum_file_size, **filename**, root_element, record_element, table_index, include_attributes, auto_detect, max_depth, unnest_as, columns | Inferred columns | **NEW:** Schema inference → structured data |

### Implementation Strategy

#### Phase 1: Create Internal Functions (No API Changes)

**File:** `src/xml_reader_functions.cpp`

```cpp
// Internal bind function for raw content (_objects functions)
static unique_ptr<FunctionData> ReadDocumentObjectsBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names,
    ParseMode mode
) {
    auto result = make_uniq<XMLReadFunctionData>();
    result->parse_mode = mode;

    // ... (unified file handling logic)

    // Handle filename parameter
    for (auto &kv : input.named_parameters) {
        if (kv.first == "ignore_errors") {
            result->ignore_errors = kv.second.GetValue<bool>();
        } else if (kv.first == "maximum_file_size") {
            result->max_file_size = kv.second.GetValue<idx_t>();
        } else if (kv.first == "filename") {
            result->include_filename = kv.second.GetValue<bool>();
        }
    }

    // Set return schema
    if (result->include_filename) {
        return_types.push_back(LogicalType::VARCHAR);
        names.push_back("filename");
    }

    // Return appropriate type based on mode
    if (mode == ParseMode::HTML) {
        return_types.push_back(XMLTypes::HTMLType());
        names.push_back("html");
    } else {
        return_types.push_back(XMLTypes::XMLType());
        names.push_back("xml");
    }

    return std::move(result);
}

// Internal bind function for structured data (schema inference)
static unique_ptr<FunctionData> ReadDocumentBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names,
    ParseMode mode
) {
    auto result = make_uniq<XMLReadFunctionData>();
    result->parse_mode = mode;

    // ... (unified file handling logic)

    // Handle schema inference parameters
    XMLSchemaOptions schema_options;
    bool has_explicit_columns = false;

    for (auto &kv : input.named_parameters) {
        // Common parameters
        if (kv.first == "ignore_errors") {
            result->ignore_errors = kv.second.GetValue<bool>();
            schema_options.ignore_errors = result->ignore_errors;
        } else if (kv.first == "maximum_file_size") {
            result->max_file_size = kv.second.GetValue<idx_t>();
            schema_options.maximum_file_size = result->max_file_size;
        } else if (kv.first == "filename") {
            result->include_filename = kv.second.GetValue<bool>();
        }
        // Schema inference parameters
        else if (kv.first == "root_element") {
            schema_options.root_element = kv.second.ToString();
        } else if (kv.first == "record_element") {
            schema_options.record_element = kv.second.ToString();
        } else if (kv.first == "include_attributes") {
            schema_options.include_attributes = kv.second.GetValue<bool>();
        } else if (kv.first == "auto_detect") {
            schema_options.auto_detect = kv.second.GetValue<bool>();
        } else if (kv.first == "max_depth") {
            schema_options.max_depth = kv.second.GetValue<int32_t>();
        } else if (kv.first == "unnest_as") {
            // ... (existing logic)
        } else if (kv.first == "columns") {
            // ... (existing explicit schema logic)
            has_explicit_columns = true;
        }
        // HTML-specific parameters
        else if (kv.first == "table_index" && mode == ParseMode::HTML) {
            schema_options.table_index = kv.second.GetValue<int32_t>();
        }
    }

    // Perform schema inference if no explicit columns
    if (!has_explicit_columns) {
        // Read first file and infer schema
        // This works for BOTH XML and HTML thanks to XMLDocRAII constructor!
        auto content = ReadFirstFile(result->files[0], fs, result->max_file_size);

        // XMLDocRAII handles both XML and HTML based on mode
        bool is_html = (mode == ParseMode::HTML);
        XMLDocRAII doc(content, is_html);

        if (doc.IsValid()) {
            auto inferred_columns = XMLSchemaInference::InferSchema(content, schema_options);
            for (const auto &col_info : inferred_columns) {
                return_types.push_back(col_info.type);
                names.push_back(col_info.name);
            }
        }
    }

    return std::move(result);
}

// Unified function implementation
static void ReadDocumentFunction(ClientContext &context, TableFunctionInput &data_p,
                                 DataChunk &output) {
    auto &bind_data = data_p.bind_data->Cast<XMLReadFunctionData>();
    auto &gstate = data_p.global_state->Cast<XMLReadGlobalState>();

    // ... (file reading loop)

    // Parse using appropriate mode
    bool is_html = (bind_data.parse_mode == ParseMode::HTML);
    XMLDocRAII doc(content, is_html);  // Infrastructure already exists!

    // Rest of logic is identical for both XML and HTML
    // ...
}
```

#### Phase 2: Refactor Public Functions to Delegate

```cpp
// read_xml_objects - delegates to internal
static unique_ptr<FunctionData> ReadXMLObjectsBind(ClientContext &context,
                                                    TableFunctionBindInput &input,
                                                    vector<LogicalType> &return_types,
                                                    vector<string> &names) {
    return ReadDocumentObjectsBind(context, input, return_types, names, ParseMode::XML);
}

// read_xml - delegates to internal
static unique_ptr<FunctionData> ReadXMLBind(ClientContext &context,
                                            TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types,
                                            vector<string> &names) {
    return ReadDocumentBind(context, input, return_types, names, ParseMode::XML);
}

// read_html_objects - delegates to internal
static unique_ptr<FunctionData> ReadHTMLObjectsBind(ClientContext &context,
                                                     TableFunctionBindInput &input,
                                                     vector<LogicalType> &return_types,
                                                     vector<string> &names) {
    return ReadDocumentObjectsBind(context, input, return_types, names, ParseMode::HTML);
}

// read_html - NOW WITH SCHEMA INFERENCE!
static unique_ptr<FunctionData> ReadHTMLBind(ClientContext &context,
                                             TableFunctionBindInput &input,
                                             vector<LogicalType> &return_types,
                                             vector<string> &names) {
    return ReadDocumentBind(context, input, return_types, names, ParseMode::HTML);
}
```

#### Phase 3: Update Function Registration

Add missing parameters to existing registrations:

```cpp
// read_xml needs filename parameter
read_xml_single.named_parameters["filename"] = LogicalType::BOOLEAN;

// read_html needs schema inference parameters (NEW!)
read_html_single.named_parameters["root_element"] = LogicalType::VARCHAR;
read_html_single.named_parameters["record_element"] = LogicalType::VARCHAR;
read_html_single.named_parameters["include_attributes"] = LogicalType::BOOLEAN;
read_html_single.named_parameters["auto_detect"] = LogicalType::BOOLEAN;
read_html_single.named_parameters["max_depth"] = LogicalType::INTEGER;
read_html_single.named_parameters["unnest_as"] = LogicalType::VARCHAR;
read_html_single.named_parameters["columns"] = LogicalType::ANY;
read_html_single.named_parameters["table_index"] = LogicalType::INTEGER;  // HTML-specific
```

## Test Requirements

### Existing Tests to Maintain

All existing tests must continue to pass:
- test/sql/xml_table_functions.test
- test/sql/read_html_table.test
- test/sql/html_file_reading.test

### New Tests Required

#### Test 1: `read_xml` with `filename` parameter

**File:** `test/sql/xml_filename_support.test`

```sql
# Test read_xml with filename parameter
query II
SELECT filename, typeof(xml)
FROM read_xml('test/xml/*.xml', filename=true)
ORDER BY filename
LIMIT 3;
----
test/xml/simple.xml  VARCHAR
...

# Test that filename appears in correct position
query I
SELECT count(*)
FROM read_xml('test/xml/simple.xml', filename=true)
WHERE filename IS NOT NULL;
----
1
```

#### Test 2: HTML with `root_element` parameter

**File:** `test/sql/html_root_element.test`

Test HTML file (`test/html/nested_tables.html`):
```html
<html>
  <body>
    <div id="data">
      <table>
        <tr><th>Name</th><th>Age</th></tr>
        <tr><td>Alice</td><td>30</td></tr>
        <tr><td>Bob</td><td>25</td></tr>
      </table>
    </div>
  </body>
</html>
```

Test queries:
```sql
# Test with root_element pointing to div containing table
query II
SELECT * FROM read_html('test/html/nested_tables.html', root_element='//div[@id="data"]');
----
Alice  30
Bob    25

# Test without root_element (should find all tables in document)
query II
SELECT * FROM read_html('test/html/nested_tables.html');
----
Alice  30
Bob    25
```

#### Test 3: HTML with `record_element` parameter

**File:** `test/sql/html_record_element.test`

Test HTML file (`test/html/list_data.html`):
```html
<html>
  <body>
    <ul class="users">
      <li data-name="Alice" data-age="30">Alice (30)</li>
      <li data-name="Bob" data-age="25">Bob (25)</li>
      <li data-name="Charlie" data-age="35">Charlie (35)</li>
    </ul>
  </body>
</html>
```

Test queries:
```sql
# Test record_element to extract individual list items
query II
SELECT * FROM read_html(
    'test/html/list_data.html',
    record_element='//li',
    include_attributes=true
);
----
Alice  30
Bob    25
Charlie 35

# Test with specific XPath for record_element
query I
SELECT count(*) FROM read_html(
    'test/html/list_data.html',
    record_element='//ul[@class="users"]/li'
);
----
3
```

#### Test 4: HTML with `table_index` parameter

**File:** `test/sql/html_table_index.test`

Test HTML file (`test/html/multiple_tables.html`):
```html
<html>
  <body>
    <table id="products">
      <tr><th>Product</th><th>Price</th></tr>
      <tr><td>Widget</td><td>10</td></tr>
    </table>

    <table id="customers">
      <tr><th>Name</th><th>City</th></tr>
      <tr><td>Alice</td><td>NYC</td></tr>
    </table>
  </body>
</html>
```

Test queries:
```sql
# Test extracting first table
query II
SELECT * FROM read_html('test/html/multiple_tables.html', table_index=0);
----
Widget  10

# Test extracting second table
query II
SELECT * FROM read_html('test/html/multiple_tables.html', table_index=1);
----
Alice  NYC

# Test default behavior (should extract first table or all tables?)
query II
SELECT * FROM read_html('test/html/multiple_tables.html');
----
Widget  10
Alice   NYC
```

#### Test 5: Schema inference consistency

**File:** `test/sql/html_xml_consistency.test`

Create matching XML and HTML files with same structure:

`test/data/people.xml`:
```xml
<people>
  <person>
    <name>Alice</name>
    <age>30</age>
  </person>
  <person>
    <name>Bob</name>
    <age>25</age>
  </person>
</people>
```

`test/data/people.html`:
```html
<table>
  <tr><th>name</th><th>age</th></tr>
  <tr><td>Alice</td><td>30</td></tr>
  <tr><td>Bob</td><td>25</td></tr>
</table>
```

Test queries:
```sql
# Both should infer similar schemas
query II
DESCRIBE SELECT * FROM read_xml('test/data/people.xml');
----
name  VARCHAR
age   VARCHAR

query II
DESCRIBE SELECT * FROM read_html('test/data/people.html');
----
name  VARCHAR
age   VARCHAR

# Both should support filename parameter
query I
SELECT typeof(filename) FROM read_xml('test/data/people.xml', filename=true) LIMIT 1;
----
VARCHAR

query I
SELECT typeof(filename) FROM read_html('test/data/people.html', filename=true) LIMIT 1;
----
VARCHAR
```

## Implementation Checklist

### Pre-Implementation
- [ ] Merge latest changes from main worktree into api-consistency-check branch
- [ ] Create test HTML files for root_element, record_element, table_index
- [ ] Document current test coverage baseline

### Phase 1: Internal Functions
- [ ] Add `ParseMode` enum to xml_reader_functions.hpp
- [ ] Update `XMLReadFunctionData` structure with parse_mode and include_filename
- [ ] Create `ReadDocumentObjectsBind` internal function
- [ ] Create `ReadDocumentBind` internal function
- [ ] Create unified `ReadDocumentFunction` implementation
- [ ] Create unified `ReadDocumentInit` implementation

### Phase 2: Refactor Existing Functions
- [ ] Refactor `ReadXMLObjectsBind` to delegate to internal
- [ ] Refactor `ReadXMLBind` to delegate to internal
- [ ] Refactor `ReadHTMLObjectsBind` to delegate to internal (rename from `ReadHTMLBind`)
- [ ] Create new `ReadHTMLBind` with schema inference (delegates to internal)
- [ ] Remove duplicate `ReadHTMLFunction` implementation

### Phase 3: Update Registration
- [ ] Add `filename` parameter to `read_xml` registration
- [ ] Add schema inference parameters to `read_html` registration
- [ ] Ensure `read_html_objects` maintains backward compatibility
- [ ] Update function documentation comments

### Phase 4: Testing
- [ ] Run existing XML tests - all must pass
- [ ] Run existing HTML tests - all must pass
- [ ] Add new test: `xml_filename_support.test`
- [ ] Add new test: `html_root_element.test`
- [ ] Add new test: `html_record_element.test`
- [ ] Add new test: `html_table_index.test`
- [ ] Add new test: `html_xml_consistency.test`
- [ ] Run full test suite with `make test`

### Phase 5: Documentation
- [ ] Update README.md with new read_html capabilities
- [ ] Update inline code comments
- [ ] Add examples for HTML schema inference
- [ ] Document migration guide for users

## Merge Strategy

Before implementing changes:

```bash
# In api-consistency-check worktree
cd /mnt/aux-data/teague/Projects/webbed-api-verify

# Fetch latest from main
git fetch origin main

# Merge main into current branch
git merge origin/main

# Resolve any conflicts
# Run tests to ensure merge is clean
make test
```

## Success Criteria

1. **API Consistency:** All four functions support the same common parameters
2. **No Code Duplication:** Single internal implementation for both XML and HTML
3. **Schema Inference:** `read_html` provides structured data extraction like `read_xml`
4. **Backward Compatibility:** All existing tests pass without modification
5. **Test Coverage:** New tests cover root_element, record_element, table_index for HTML
6. **Documentation:** Users understand new capabilities and migration path

## Risk Assessment

### Low Risk
- Adding `filename` parameter to `read_xml` (additive change)
- Creating internal functions (no API change)
- HTML schema inference (new capability, opt-in)

### Medium Risk
- Changing `read_html` behavior (currently returns raw HTML)
  - **Mitigation:** Ensure backward compatibility with existing usage
  - **Note:** Current `read_html` and `read_html_objects` are identical, so no breaking change

### High Risk
- None identified (refactoring is internal, API remains compatible)

## Timeline Estimate

- Phase 1 (Internal Functions): 2-3 hours
- Phase 2 (Refactoring): 1-2 hours
- Phase 3 (Registration): 30 minutes
- Phase 4 (Testing): 2-3 hours
- Phase 5 (Documentation): 1 hour

**Total:** 6-9 hours of focused development time

## Notes

- The infrastructure (XMLDocRAII with HTML support) already exists
- Schema inference code is parser-agnostic after document is loaded
- Main work is refactoring, not new functionality
- Test creation is the most time-consuming part

## Open Questions

1. Should `read_html` default to extracting all tables or require `table_index`?
   - **Recommendation:** Extract all tables by default, use `table_index` to select specific one

2. Should HTML schema inference default to table extraction or generic element extraction?
   - **Recommendation:** Default to table extraction (most common use case), use `record_element` for custom extraction

3. How should multiple tables in one HTML file be represented?
   - **Option A:** Append all rows with table_index column
   - **Option B:** User must specify table_index
   - **Recommendation:** Option A (matches multi-file behavior)

## References

- xml_reader_functions.cpp:16-163 - `read_xml_objects`
- xml_reader_functions.cpp:165-432 - `read_xml`
- xml_reader_functions.cpp:561-793 - HTML functions
- xml_utils.cpp:65-92 - `XMLDocRAII` constructor with HTML support
- xml_schema_inference.cpp - Schema inference logic (parser-agnostic)
