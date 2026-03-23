# Task: Add `nullstr` parameter for custom NULL value representation (#40)

## Goal
Add a `nullstr` parameter to `read_xml` and `read_html` that lets users specify string values to interpret as NULL, matching DuckDB CSV extension behavior.

## API
```sql
-- Single null representation
SELECT * FROM read_xml('data.xml', nullstr='N/A');

-- Multiple null representations
SELECT * FROM read_xml('data.xml', nullstr=['NULL', 'N/A', '-', '']);

-- Also for read_html
SELECT * FROM read_html('data.html', nullstr=['', 'N/A']);
```

## Implementation

### 1. Add option to XMLSchemaOptions (`src/include/xml_schema_inference.hpp`)
```cpp
// In struct XMLSchemaOptions:
std::vector<std::string> null_strings; // Values to treat as NULL (empty = default behavior)
```

### 2. Register parameter in bind functions (`src/xml_reader_functions.cpp`)
Add `nullstr` parameter to both `read_xml` and `read_html` bind functions. Accept both a single VARCHAR and a LIST(VARCHAR). Parse into `XMLSchemaOptions::null_strings`.

Look at existing parameter parsing in the bind functions for patterns (e.g., how `force_list` is parsed).

### 3. Apply during data extraction (`src/xml_schema_inference.cpp`)

In `ConvertToValue()` and/or `ExtractValueFromNode()`: after extracting text content, check if the cleaned text matches any entry in `null_strings`. If so, return `Value(LogicalType)` (typed NULL) instead of the string value.

### 4. Apply during schema inference
In `InferTypeFromSamples()`: exclude values matching `null_strings` from type inference samples, so `N/A` doesn't force a column to VARCHAR.

### 5. Interaction with `all_varchar`
When `all_varchar=true`, nullstr matching should still apply — matched values become NULL even in VARCHAR columns.

### 6. Interaction with `empty_elements`
- `empty_elements='null'` already handles `<value/>` and `<value></value>` → these remain NULL regardless of nullstr
- `nullstr` handles text content like `<value>N/A</value>` — these are different concerns

## Tests (`test/sql/`)
Create `test/sql/nullstr_parameter.test`:
1. Single nullstr value
2. Multiple nullstr values (list syntax)
3. Nullstr affects type inference (column with 'N/A' and numbers becomes BIGINT, not VARCHAR)
4. Nullstr with `all_varchar=true`
5. Nullstr doesn't affect non-matching values
6. Works with both read_xml and read_html
7. Default behavior unchanged when nullstr not specified
8. Case sensitivity (nullstr should be case-sensitive by default)

## Files to modify
- `src/include/xml_schema_inference.hpp` — add field to XMLSchemaOptions
- `src/xml_reader_functions.cpp` — register parameter in bind functions
- `src/xml_schema_inference.cpp` — apply in ConvertToValue, InferTypeFromSamples
- `README.md` — document the parameter
- `docs/parameters.rst` — add to parameter table
- `test/sql/nullstr_parameter.test` — new test file

## Test data
Create `test/xml/nullstr_test.xml`:
```xml
<?xml version="1.0"?>
<records>
  <record>
    <name>Alice</name>
    <score>95</score>
    <grade>A</grade>
  </record>
  <record>
    <name>Bob</name>
    <score>N/A</score>
    <grade>-</grade>
  </record>
  <record>
    <name>Charlie</name>
    <score>87</score>
    <grade>NULL</grade>
  </record>
</records>
```
