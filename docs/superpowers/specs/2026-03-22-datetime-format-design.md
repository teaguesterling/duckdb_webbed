# Design: `datetime_format` Parameter for Custom Date/Time Parsing (#38)

## Goal

Add a `datetime_format` parameter to `read_xml`, `read_html`, `parse_xml`, and `parse_html` that lets users specify expected date/time format strings. Replace the current regex-based auto-detection with a `StrpTimeFormat`-based candidate elimination approach.

## API

```sql
-- Auto-detect (default) — tries a built-in list of common formats
SELECT * FROM read_xml('data.xml');
SELECT * FROM read_xml('data.xml', datetime_format='auto');

-- Disable temporal detection entirely
SELECT * FROM read_xml('data.xml', datetime_format='none');

-- Explicit format string
SELECT * FROM read_xml('data.xml', datetime_format='%m/%d/%Y');

-- Preset names
SELECT * FROM read_xml('data.xml', datetime_format='us');
SELECT * FROM read_xml('data.xml', datetime_format='eu');

-- List of formats (candidates — survivors determine column type)
SELECT * FROM read_xml('data.xml', datetime_format=['%m/%d/%Y', '%Y-%m-%d %H:%M:%S']);
```

Parameter type: `VARCHAR` or `LIST(VARCHAR)`. Preset names are recognized both as standalone strings and inside lists (e.g., `datetime_format=['us', 'iso_timestamp']`).

Available on: `read_xml`, `read_html`, `parse_xml`, `parse_html`.

## Presets

| Name | Expands To | Target Type |
|------|-----------|-------------|
| `auto` | Built-in candidate list (see below) | mixed |
| `none` | Empty list — disables detection | — |
| `us` | `%m/%d/%Y` | DATE |
| `eu` | `%d/%m/%Y` | DATE |
| `iso` | `%Y-%m-%d` | DATE |
| `us_timestamp` | `%m/%d/%Y %I:%M:%S %p` | TIMESTAMP |
| `eu_timestamp` | `%d/%m/%Y %H:%M:%S` | TIMESTAMP |
| `iso_timestamp` | `%Y-%m-%dT%H:%M:%S` | TIMESTAMP |
| `iso_timestamptz` | `%Y-%m-%dT%H:%M:%S%z` | TIMESTAMPTZ |
| `12hour` | `%I:%M:%S %p` | TIME |
| `24hour` | `%H:%M:%S` | TIME |

### Auto-detect candidate list (priority order)

When `datetime_format='auto'` (or omitted), the following formats are tried. Order establishes priority for ambiguous values — first surviving format wins.

**Dates:**
1. `%Y-%m-%d` (ISO — unambiguous, highest priority)
2. `%m/%d/%Y` (US — DuckDB default for ambiguous `XX/XX/XXXX`)
3. `%d/%m/%Y` (EU — only wins if US eliminated by invalid month)
4. `%Y/%m/%d`

**Timestamps:**
1. `%Y-%m-%dT%H:%M:%S.%f%z` (ISO with fractional seconds and timezone)
2. `%Y-%m-%dT%H:%M:%S%z` (ISO with timezone)
3. `%Y-%m-%dT%H:%M:%S.%f` (ISO with fractional seconds)
4. `%Y-%m-%dT%H:%M:%S` (ISO)
5. `%Y-%m-%d %H:%M:%S.%f%z`
6. `%Y-%m-%d %H:%M:%S%z`
7. `%Y-%m-%d %H:%M:%S.%f`
8. `%Y-%m-%d %H:%M:%S`

**Times:**
1. `%H:%M:%S` (24-hour)
2. `%I:%M:%S %p` (12-hour)
3. `%H:%M`

> **Ambiguity note:** When auto-detecting, ambiguous date formats (e.g., `03/04/2024`) default to US (month-first) ordering, consistent with DuckDB conventions. Use `datetime_format='eu'` to override.

## Implementation

### Approach: StrpTimeFormat-based candidate elimination

Uses DuckDB's built-in `StrpTimeFormat` for both detection and parsing. Replaces the current regex-based `IsDate()`/`IsTimestamp()`/`IsTime()` detection.

### XMLSchemaOptions changes (`xml_schema_inference.hpp`)

```cpp
// In struct XMLSchemaOptions:
std::vector<std::string> datetime_format_candidates;  // Resolved format strings
bool has_explicit_datetime_format = false;             // User specified a format (not 'auto')
```

The existing `bool temporal_detection` field is deprecated — `datetime_format='none'` replaces it. If both are set, `datetime_format` takes precedence. The `temporal_detection` field is kept for backward compatibility but ignored when `datetime_format` is specified.

Per-column format storage: add a `std::string winning_datetime_format` field to the column info (or equivalent per-column structure) so that `ConvertToValue()` knows which format to use for each column.

### Parameter registration (`xml_reader_functions.cpp`)

Register `datetime_format` as `LogicalType::ANY` (accepts VARCHAR or LIST(VARCHAR)) on all four functions. In bind:
- Resolve preset names to format string lists
- Single string → wrap in list
- Validate format strings via `StrTimeFormat::ParseFormatSpecifier`
- Error at bind time for invalid format strings

### Detection flow (`xml_schema_inference.cpp`)

Replace `IsDate()`, `IsTimestamp()`, `IsTime()` with a unified `StrpTimeFormat`-based approach:

1. **Each column maintains its own independent set of candidate formats.** Candidates are not shared across columns — a date column eliminating time formats does not affect a timestamp column.
2. For each sample value, try each candidate format using `StrpTimeFormat::Parse()` then `ParseResult::TryToDate()`, `TryToTimestamp()`, or `TryToTime()` depending on the format's type classification.
3. **Empty values and nulls (including `nullstr` matches) skip elimination** — they do not eliminate any candidate.
4. Eliminate candidates that fail on any non-null sample.
5. Surviving candidates determine column type:
   - Format has only date specifiers → DATE
   - Format has time specifiers with `%z`/`%Z` → TIMESTAMPTZ
   - Format has time specifiers → TIMESTAMP (or TIME if no date specifiers)
   - No survivors → continue to numeric/boolean/VARCHAR detection
6. **If multiple candidates survive, the first in list order wins** (priority order for auto-detect, user-provided order for explicit lists).
7. Store the winning format with the column for use during conversion.
8. **Mixed-format columns** (e.g., some rows `2024-01-15`, others `01/15/2024`): all candidates will be eliminated, column falls back to VARCHAR. This is intentional — per-row format switching is not supported.

### Conversion (`ConvertToValue()`)

When a column has an associated format (from detection or explicit specification):
- Parse with the winning `StrpTimeFormat` instance
- On failure: throw error by default, return NULL if `ignore_errors=true`

### Type determination from format string

Determined once at bind time by inspecting format specifiers:
- Contains `%H`, `%I`, `%M`, `%S`, `%f`, `%g`, `%p` → has time components
- Contains `%z`, `%Z` → has timezone
- Contains `%Y`, `%m`, `%d` (or equivalents) → has date components

| Date | Time | TZ | Result Type |
|------|------|----|-------------|
| yes  | no   | no | DATE |
| no   | yes  | no | TIME |
| no   | yes  | yes| TIMETZ |
| yes  | yes  | no | TIMESTAMP |
| yes  | yes  | yes| TIMESTAMPTZ |
| other combinations | | | Error at bind time |

Formats that don't resolve to a clear type (e.g., `%Y` alone) produce a bind-time error: "datetime_format '%Y' does not specify a complete date, time, or timestamp."

## Interaction with other options

- `all_varchar=true` → overrides `datetime_format`, no temporal detection
- `datetime_format='none'` → disables temporal detection, all temporal-looking values stay VARCHAR
- `nullstr` → applied before datetime parsing (check for null first)

## Error handling

- **User-specified format, value doesn't match:** error by default, NULL with `ignore_errors=true`
- **Auto-detected format, value doesn't match:** same — once a format wins inference, it's authoritative
- **Invalid format string:** error at bind time
- **`all_varchar=true` with `datetime_format`:** `all_varchar` wins silently

## Tests (`test/sql/datetime_format.test`)

1. Explicit date format (`%m/%d/%Y`) parses US dates correctly
2. Explicit timestamp format with timezone → TIMESTAMPTZ
3. Explicit time format (`%H:%M:%S`) → TIME
4. Preset names (`us`, `eu`, `iso`, `12hour`, `24hour`, etc.)
5. List of formats — first surviving format wins
6. Auto-detect default unchanged (ISO dates still work)
7. Auto-detect eliminates ambiguous formats correctly
8. `datetime_format='none'` disables temporal detection
9. Non-matching value with explicit format → error
10. Non-matching value with `ignore_errors=true` → NULL
11. `all_varchar=true` overrides `datetime_format`
12. Invalid format string → bind-time error
13. Works on `read_xml`, `read_html`, `parse_xml`, `parse_html`

## Files to modify

- `src/include/xml_schema_inference.hpp` — add fields to XMLSchemaOptions
- `src/xml_reader_functions.cpp` — register parameter, resolve presets in bind
- `src/xml_schema_inference.cpp` — replace IsDate/IsTimestamp/IsTime with StrpTimeFormat detection, update ConvertToValue (also add missing `case LogicalTypeId::TIME` to ConvertToValue — pre-existing gap)
- `test/sql/datetime_format.test` — new test file
- `docs/parameters.rst` — document the parameter

## Future extensions

These are explicitly out of scope but the design accommodates them:

- **Struct form:** `datetime_format={date: '%m/%d/%Y', timestamp: '%m/%d/%Y %H:%M:%S'}` for separate date/timestamp formats
- **Per-element mapping:** `datetime_format={'/events/event/date': '%m/%d/%Y'}` for XPath-based format assignment
- **Interval/duration support:** Would require different parsing infrastructure (`Interval::FromString()`)
