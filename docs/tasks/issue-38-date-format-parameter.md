# Task: Add `date_format` parameter for custom date/time parsing (#38)

## Goal
Add a `date_format` parameter to `read_xml` and `read_html` that lets users specify expected date/time format strings, and improve auto-detection for common non-ISO formats.

## API
```sql
-- Single format
SELECT * FROM read_xml('data.xml', date_format='%m/%d/%Y');

-- Multiple formats (try in order)
SELECT * FROM read_xml('data.xml', date_format=['%Y-%m-%d', '%m/%d/%Y']);

-- Timestamp format
SELECT * FROM read_xml('data.xml', timestamp_format='%Y-%m-%d %H:%M:%S%z');
```

## Implementation

### 1. Add options to XMLSchemaOptions (`src/include/xml_schema_inference.hpp`)
```cpp
// In struct XMLSchemaOptions:
std::vector<std::string> date_formats;      // User-specified date formats (empty = auto-detect)
std::vector<std::string> timestamp_formats;  // User-specified timestamp formats (empty = auto-detect)
```

### 2. Register parameters in bind functions (`src/xml_reader_functions.cpp`)
Add `date_format` and `timestamp_format` parameters to both `read_xml` and `read_html`. Accept both VARCHAR and LIST(VARCHAR).

### 3. Modify type detection (`src/xml_schema_inference.cpp`)

#### When user provides formats:
- In `IsDate()` / `IsTimestamp()`: if user formats are specified, try `StrpTimeFormat::Parse()` with each format. Accept only values matching at least one user format.
- Reference: DuckDB's `StrpTimeFormat` in `duckdb/src/common/types/date.cpp` or `duckdb/src/function/scalar/strftime_format.cpp`

#### For auto-detection improvements:
- Add timezone offset support to existing timestamp patterns: `YYYY-MM-DDTHH:MM:SS+HH:MM`
- Current patterns in `IsDate()` / `IsTimestamp()` (around lines 1039-1087) use regex — extend these

### 4. Apply during data extraction
In `ConvertToValue()`: when converting a string to DATE or TIMESTAMP, if user formats are specified, use `StrpTimeFormat::Parse()` with those formats instead of DuckDB's default `Date::FromString()` / `Timestamp::FromString()`.

### 5. Interaction with other options
- `all_varchar=true` → skip date parsing entirely (existing behavior)
- `temporal_detection=false` → skip auto-detection but still honor explicit `date_format` if provided
- `nullstr` (#40) → apply nullstr check before date format parsing

## Tests (`test/sql/`)
Create `test/sql/date_format_parameter.test`:
1. User-specified date format (`%m/%d/%Y`)
2. User-specified timestamp format with timezone
3. Multiple formats (first match wins)
4. Format doesn't match → stays VARCHAR
5. Works with read_xml and read_html
6. Default behavior unchanged
7. Interaction with `temporal_detection=false`
8. Interaction with `all_varchar=true`

## Files to modify
- `src/include/xml_schema_inference.hpp` — add fields to XMLSchemaOptions
- `src/xml_reader_functions.cpp` — register parameters in bind functions
- `src/xml_schema_inference.cpp` — modify IsDate/IsTimestamp/ConvertToValue
- `README.md` — document the parameters
- `docs/parameters.rst` — add to parameter table
- `test/sql/date_format_parameter.test` — new test file

## Test data
Create `test/xml/date_format_test.xml`:
```xml
<?xml version="1.0"?>
<events>
  <event>
    <name>Meeting</name>
    <date>03/15/2024</date>
    <timestamp>2024-03-15T10:30:00-05:00</timestamp>
  </event>
  <event>
    <name>Conference</name>
    <date>12/25/2024</date>
    <timestamp>2024-12-25T09:00:00+01:00</timestamp>
  </event>
</events>
```

## Notes
- DuckDB's `StrpTimeFormat` class handles format string parsing — look in `duckdb/src/include/duckdb/common/types/strftime.hpp`
- The CSV reader's date_format handling in `duckdb/extension/core_functions/` is a good reference
