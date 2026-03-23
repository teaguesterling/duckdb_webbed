# datetime_format Parameter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `datetime_format` parameter to read_xml/read_html/parse_xml/parse_html that uses DuckDB's StrpTimeFormat for date/time detection and parsing, replacing the current regex approach.

**Architecture:** A single `datetime_format` parameter accepts VARCHAR or LIST(VARCHAR). Values are resolved to a list of StrpTimeFormat candidate strings at bind time (presets like 'auto'/'us'/'eu' expand to predefined lists). During schema inference, candidates are tested per-column against sample values and eliminated; the surviving format determines the column type. During data extraction, the winning format parses values via StrpTimeFormat.

**Tech Stack:** DuckDB StrpTimeFormat (`duckdb/function/scalar/strftime_format.hpp`), libxml2

**Spec:** `docs/superpowers/specs/2026-03-22-datetime-format-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/include/xml_schema_inference.hpp` | Modify | Add datetime_format fields to XMLSchemaOptions, winning_datetime_format to XMLColumnInfo |
| `src/xml_schema_inference.cpp` | Modify | Replace IsDate/IsTimestamp/IsTime with StrpTimeFormat detection, update ConvertToValue |
| `src/xml_reader_functions.cpp` | Modify | Register parameter, parse/resolve presets in 3 bind functions |
| `test/xml/datetime_format_test.xml` | Create | Test data with US-formatted dates |
| `test/xml/datetime_format_iso.xml` | Create | Test data with ISO dates (auto-detect baseline) |
| `test/sql/xml_datetime_format.test` | Create | All datetime_format tests |
| `docs/parameters.rst` | Modify | Document datetime_format parameter |

---

## Task 1: Add datetime_format fields to XMLSchemaOptions and XMLColumnInfo

**Files:**
- Modify: `src/include/xml_schema_inference.hpp:1-7` (includes), `src/include/xml_schema_inference.hpp:39-42` (type detection fields), `src/include/xml_schema_inference.hpp:63-75` (XMLColumnInfo)

- [ ] **Step 1: Add include for StrpTimeFormat**

In `src/include/xml_schema_inference.hpp`, add after the existing includes (line 6):

```cpp
#include "duckdb/function/scalar/strftime_format.hpp"
```

- [ ] **Step 2: Add datetime_format fields to XMLSchemaOptions**

In `src/include/xml_schema_inference.hpp`, after the existing type detection fields (after line 42), add:

```cpp
	// Datetime format (replaces temporal_detection for explicit format control)
	std::vector<std::string> datetime_format_candidates; // Resolved format strings (empty = use temporal_detection flag)
	bool has_explicit_datetime_format = false;            // User specified a format (not 'auto')
```

- [ ] **Step 3: Add winning_datetime_format to XMLColumnInfo**

In `src/include/xml_schema_inference.hpp`, in the XMLColumnInfo struct (after line 69), add a field:

```cpp
	std::string winning_datetime_format; // Format string that won during inference (empty = no format)
```

And update the constructor to initialize it (it will default to empty string).

- [ ] **Step 4: Verify the project still compiles**

Run: `make release GEN=ninja VCPKG_TOOLCHAIN_PATH=$(pwd)/vcpkg/scripts/buildsystems/vcpkg.cmake`
Expected: Clean compilation with no errors.

- [ ] **Step 5: Commit**

```bash
git add src/include/xml_schema_inference.hpp
git commit -m "feat(#38): add datetime_format fields to XMLSchemaOptions and XMLColumnInfo"
```

---

## Task 2: Create test data files

**Files:**
- Create: `test/xml/datetime_format_test.xml`
- Create: `test/xml/datetime_format_iso.xml`

- [ ] **Step 1: Create US-format date test data**

Create `test/xml/datetime_format_test.xml`:

```xml
<?xml version="1.0"?>
<events>
  <event>
    <name>Meeting</name>
    <date>03/15/2024</date>
    <time>10:30:00</time>
  </event>
  <event>
    <name>Conference</name>
    <date>12/25/2024</date>
    <time>09:00:00</time>
  </event>
</events>
```

- [ ] **Step 2: Create ISO-format date test data**

Create `test/xml/datetime_format_iso.xml`:

```xml
<?xml version="1.0"?>
<events>
  <event>
    <name>Meeting</name>
    <date>2024-03-15</date>
    <timestamp>2024-03-15T10:30:00</timestamp>
  </event>
  <event>
    <name>Conference</name>
    <date>2024-12-25</date>
    <timestamp>2024-12-25T09:00:00</timestamp>
  </event>
</events>
```

- [ ] **Step 3: Commit**

```bash
git add test/xml/datetime_format_test.xml test/xml/datetime_format_iso.xml
git commit -m "test(#38): add test data files for datetime_format parameter"
```

---

## Task 3: Register datetime_format parameter in bind functions

**Files:**
- Modify: `src/xml_reader_functions.cpp:1658-1679` (read_xml_single params)
- Modify: `src/xml_reader_functions.cpp:1685-1706` (read_xml_array params)
- Modify: `src/xml_reader_functions.cpp:1715-1737` (read_html_single params)
- Modify: `src/xml_reader_functions.cpp:1740-1764` (read_html_array params)
- Modify: `src/xml_reader_functions.cpp:1814-1831` (parse_xml params)
- Modify: `src/xml_reader_functions.cpp:1836-1853` (parse_html params)

- [ ] **Step 1: Add datetime_format to all parameter registration blocks**

Add the following line to each of the 6 parameter registration blocks (after the `all_varchar` line in each):

```cpp
	<function_var>.named_parameters["datetime_format"] = LogicalType::ANY; // VARCHAR or LIST(VARCHAR)
```

Where `<function_var>` is `read_xml_single`, `read_xml_array`, `read_html_single`, `read_html_array`, `parse_xml`, `parse_html`.

- [ ] **Step 2: Add preset resolution helper function**

Add a static helper function near the top of `src/xml_reader_functions.cpp` (before the bind functions, around line 18):

```cpp
#include "duckdb/function/scalar/strftime_format.hpp"

namespace {

// Resolve a datetime_format preset name or format string to a list of format strings
std::vector<std::string> ResolveDatetimeFormat(const std::string &input) {
	// Preset name mappings
	if (input == "auto") {
		return {
		    // Timestamps with fractional seconds + timezone (most specific first)
		    "%Y-%m-%dT%H:%M:%S.%f%z",
		    "%Y-%m-%dT%H:%M:%S%z",
		    "%Y-%m-%dT%H:%M:%S.%f",
		    "%Y-%m-%dT%H:%M:%S",
		    "%Y-%m-%d %H:%M:%S.%f%z",
		    "%Y-%m-%d %H:%M:%S%z",
		    "%Y-%m-%d %H:%M:%S.%f",
		    "%Y-%m-%d %H:%M:%S",
		    // Dates (ISO > US > EU)
		    "%Y-%m-%d",
		    "%m/%d/%Y",
		    "%d/%m/%Y",
		    "%Y/%m/%d",
		    // Times
		    "%H:%M:%S",
		    "%I:%M:%S %p",
		    "%H:%M",
		};
	}
	if (input == "none") {
		return {};
	}
	if (input == "us") {
		return {"%m/%d/%Y"};
	}
	if (input == "eu") {
		return {"%d/%m/%Y"};
	}
	if (input == "iso") {
		return {"%Y-%m-%d"};
	}
	if (input == "us_timestamp") {
		return {"%m/%d/%Y %I:%M:%S %p"};
	}
	if (input == "eu_timestamp") {
		return {"%d/%m/%Y %H:%M:%S"};
	}
	if (input == "iso_timestamp") {
		return {"%Y-%m-%dT%H:%M:%S"};
	}
	if (input == "iso_timestamptz") {
		return {"%Y-%m-%dT%H:%M:%S%z"};
	}
	if (input == "12hour") {
		return {"%I:%M:%S %p"};
	}
	if (input == "24hour") {
		return {"%H:%M:%S"};
	}
	// Not a preset — treat as a format string
	return {input};
}

// Classify a format string to determine what DuckDB type it produces
LogicalType ClassifyDatetimeFormat(const std::string &format) {
	bool has_date = false;
	bool has_time = false;
	bool has_tz = false;

	// Scan for specifier characters after '%'
	for (size_t i = 0; i < format.size(); i++) {
		if (format[i] == '%' && i + 1 < format.size()) {
			char spec = format[i + 1];
			switch (spec) {
			case 'Y': case 'y': case 'm': case 'd': case 'j':
			case 'U': case 'W': case 'G': case 'V': case 'u':
			case 'a': case 'A': case 'b': case 'B': case 'w':
			case 'x': case 'c':
				has_date = true;
				break;
			case 'H': case 'I': case 'M': case 'S': case 'f':
			case 'g': case 'p': case 'X': case 'n':
				has_time = true;
				break;
			case 'z': case 'Z':
				has_tz = true;
				has_time = true; // timezone implies time
				break;
			default:
				break;
			}
			i++; // skip specifier char
		}
	}

	if (has_date && has_time && has_tz) {
		return LogicalType::TIMESTAMP_TZ;
	}
	if (has_date && has_time) {
		return LogicalType::TIMESTAMP;
	}
	if (has_date) {
		return LogicalType::DATE;
	}
	if (has_time && has_tz) {
		return LogicalType::TIME_TZ;
	}
	if (has_time) {
		return LogicalType::TIME;
	}
	throw BinderException("datetime_format '%s' does not specify a complete date, time, or timestamp.", format);
}

// Validate that a format string is parseable by StrpTimeFormat
void ValidateDatetimeFormatString(const std::string &format) {
	duckdb::StrpTimeFormat strp_format;
	string error = StrTimeFormat::ParseFormatSpecifier(format, strp_format);
	if (!error.empty()) {
		throw BinderException("Invalid datetime_format '%s': %s", format, error);
	}
}

} // anonymous namespace
```

- [ ] **Step 3: Add datetime_format parsing to ReadXMLBind parameter loop**

In `src/xml_reader_functions.cpp`, in the ReadXMLBind parameter loop (after the `all_varchar` handling around line 920), add:

```cpp
		} else if (kv.first == "datetime_format") {
			// Handle both VARCHAR and LIST(VARCHAR)
			std::vector<std::string> all_formats;
			if (kv.second.type().id() == LogicalTypeId::VARCHAR) {
				auto resolved = ResolveDatetimeFormat(kv.second.ToString());
				all_formats.insert(all_formats.end(), resolved.begin(), resolved.end());
			} else if (kv.second.type().id() == LogicalTypeId::LIST) {
				auto &list_children = ListValue::GetChildren(kv.second);
				for (const auto &child : list_children) {
					if (!child.IsNull() && child.type().id() == LogicalTypeId::VARCHAR) {
						auto resolved = ResolveDatetimeFormat(child.ToString());
						all_formats.insert(all_formats.end(), resolved.begin(), resolved.end());
					}
				}
			}
			// Validate all format strings
			for (const auto &fmt : all_formats) {
				ValidateDatetimeFormatString(fmt);
			}
			schema_options.datetime_format_candidates = all_formats;
			// 'auto' is the default — only mark explicit if user specified something else
			// Check: if the original input was "auto", don't mark as explicit
			bool is_auto = false;
			if (kv.second.type().id() == LogicalTypeId::VARCHAR && kv.second.ToString() == "auto") {
				is_auto = true;
			}
			schema_options.has_explicit_datetime_format = !is_auto;
			// If 'none' was specified, disable temporal detection
			if (all_formats.empty()) {
				schema_options.temporal_detection = false;
			}
			// Explicit datetime_format overrides temporal_detection=false
			if (!all_formats.empty()) {
				schema_options.temporal_detection = true;
			}
```

- [ ] **Step 4: Add same datetime_format parsing to ReadDocumentBind parameter loop**

In `src/xml_reader_functions.cpp`, in the `ReadDocumentBind` parameter loop (after `all_varchar` around line 248), add the same `datetime_format` handling block from Step 3. Note: `ReadDocumentBind` (line 112) is the shared bind function used by both `ReadXMLBind` (read_xml) and `ReadHTMLBind` (read_html via delegation at line 1256).

- [ ] **Step 5: Add same datetime_format parsing to ParseDocumentBind parameter loop**

In `src/xml_reader_functions.cpp`, in the ParseDocumentBind parameter loop (after `all_varchar` around line 1446), add the same `datetime_format` handling block from Step 3.

- [ ] **Step 6: Verify compilation**

Run: `make release GEN=ninja VCPKG_TOOLCHAIN_PATH=$(pwd)/vcpkg/scripts/buildsystems/vcpkg.cmake`
Expected: Clean compilation.

- [ ] **Step 7: Commit**

```bash
git add src/xml_reader_functions.cpp
git commit -m "feat(#38): register datetime_format parameter with preset resolution"
```

---

## Task 4: Replace regex detection with StrpTimeFormat-based detection

**Files:**
- Modify: `src/xml_schema_inference.cpp:1-8` (includes)
- Modify: `src/xml_schema_inference.cpp:900-942` (InferTypeFromSamples)
- Modify: `src/xml_schema_inference.cpp:1039-1087` (IsDate/IsTime/IsTimestamp)

- [ ] **Step 1: Write failing test for explicit US date format**

Create `test/sql/xml_datetime_format.test` with:

```
# name: test/sql/xml_datetime_format.test
# description: Test datetime_format parameter for custom date/time parsing
# group: [sql]

require webbed

# Test 1: Explicit US date format should parse dates correctly
query I
SELECT typeof(date) FROM read_xml('test/xml/datetime_format_test.xml', datetime_format='us') LIMIT 1;
----
DATE

query II
SELECT name, date FROM read_xml('test/xml/datetime_format_test.xml', datetime_format='us') ORDER BY name;
----
Conference	2024-12-25
Meeting	2024-03-15
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test` (or the specific test runner)
Expected: FAIL — datetime_format parameter not recognized or dates not parsed as DATE.

- [ ] **Step 3: Add StrpTimeFormat include to xml_schema_inference.cpp**

In `src/xml_schema_inference.cpp`, add after existing includes (line 4):

```cpp
#include "duckdb/function/scalar/strftime_format.hpp"
```

- [ ] **Step 4: Add StrpTimeFormat-based detection helper**

In `src/xml_schema_inference.cpp`, add a new helper function before `InferTypeFromSamples` (before line 900). This replaces the regex-based IsDate/IsTimestamp/IsTime for format-aware detection:

```cpp
// Try to match a value against a StrpTimeFormat candidate
// Returns true if the value can be parsed with the given format
static bool TryMatchDatetimeFormat(const std::string &value, const std::string &format_str,
                                   const LogicalType &expected_type) {
	StrpTimeFormat strp_format;
	StrTimeFormat::ParseFormatSpecifier(format_str, strp_format);

	switch (expected_type.id()) {
	case LogicalTypeId::DATE: {
		date_t result;
		return strp_format.TryParseDate(value.c_str(), value.size(), result);
	}
	case LogicalTypeId::TIMESTAMP:
	case LogicalTypeId::TIMESTAMP_TZ: {
		timestamp_t result;
		return strp_format.TryParseTimestamp(value.c_str(), value.size(), result);
	}
	case LogicalTypeId::TIME:
	case LogicalTypeId::TIME_TZ: {
		StrpTimeFormat::ParseResult parse_result;
		if (!strp_format.Parse(value.c_str(), value.size(), parse_result)) {
			return false;
		}
		dtime_t time_result;
		return parse_result.TryToTime(time_result);
	}
	default:
		return false;
	}
}
```

- [ ] **Step 5: Rewrite InferTypeFromSamples with candidate elimination approach**

Replace the entire `InferTypeFromSamples` function (lines 900-942) with a candidate elimination approach. This goes directly to the final design (no intermediate per-sample approach):

```cpp
LogicalType XMLSchemaInference::InferTypeFromSamples(const std::vector<std::string> &samples,
                                                     const XMLSchemaOptions &options,
                                                     std::string &winning_datetime_format) {
	winning_datetime_format.clear();

	if (options.all_varchar) {
		return LogicalType::VARCHAR;
	}
	if (samples.empty()) {
		return LogicalType::VARCHAR;
	}

	// Build effective candidate list
	const auto &candidates = options.datetime_format_candidates;
	static const std::vector<std::string> auto_candidates = {
	    "%Y-%m-%dT%H:%M:%S.%f%z", "%Y-%m-%dT%H:%M:%S%z",
	    "%Y-%m-%dT%H:%M:%S.%f",   "%Y-%m-%dT%H:%M:%S",
	    "%Y-%m-%d %H:%M:%S.%f%z", "%Y-%m-%d %H:%M:%S%z",
	    "%Y-%m-%d %H:%M:%S.%f",   "%Y-%m-%d %H:%M:%S",
	    "%Y-%m-%d",                "%m/%d/%Y",
	    "%d/%m/%Y",                "%Y/%m/%d",
	    "%H:%M:%S",                "%I:%M:%S %p",
	    "%H:%M",
	};
	const auto &effective_candidates =
	    (!candidates.empty()) ? candidates :
	    (options.temporal_detection ? auto_candidates : candidates); // empty if temporal_detection=false

	// Per-column candidate elimination for datetime formats
	// Each column maintains its own independent set of candidates
	// Start with all candidates alive, eliminate those that fail on any non-empty sample
	std::vector<bool> alive(effective_candidates.size(), true);
	std::vector<LogicalType> candidate_types;
	for (const auto &fmt : effective_candidates) {
		candidate_types.push_back(ClassifyDatetimeFormat(fmt));
	}

	bool any_alive = !effective_candidates.empty();
	for (const auto &sample : samples) {
		if (sample.empty()) {
			continue; // Nulls/empty skip elimination
		}
		for (size_t i = 0; i < effective_candidates.size(); i++) {
			if (!alive[i]) continue;
			if (!TryMatchDatetimeFormat(sample, effective_candidates[i], candidate_types[i])) {
				alive[i] = false;
			}
		}
		// Early exit if all candidates eliminated
		any_alive = false;
		for (bool a : alive) {
			if (a) { any_alive = true; break; }
		}
		if (!any_alive) break;
	}

	// Find first surviving candidate (priority = list order)
	if (any_alive) {
		for (size_t i = 0; i < effective_candidates.size(); i++) {
			if (alive[i]) {
				winning_datetime_format = effective_candidates[i];
				return candidate_types[i];
			}
		}
	}

	// Fall through to numeric/boolean/VARCHAR detection
	std::vector<LogicalType> detected_types;
	for (const auto &sample : samples) {
		if (sample.empty()) continue;
		if (options.numeric_detection && IsInteger(sample)) {
			detected_types.push_back(LogicalType::INTEGER);
		} else if (options.numeric_detection && IsDouble(sample)) {
			detected_types.push_back(LogicalType::DOUBLE);
		} else if (options.boolean_detection && IsBoolean(sample)) {
			detected_types.push_back(LogicalType::BOOLEAN);
		} else {
			detected_types.push_back(LogicalType::VARCHAR);
		}
	}

	return GetMostSpecificType(detected_types);
}
```

Also add `ClassifyDatetimeFormat` and `ValidateDatetimeFormatString` as static methods on `XMLSchemaInference` in `xml_schema_inference.hpp` (move from anonymous namespace in xml_reader_functions.cpp). Update xml_reader_functions.cpp to call `XMLSchemaInference::ClassifyDatetimeFormat()` and `XMLSchemaInference::ValidateDatetimeFormatString()`.

Update the declaration in `xml_schema_inference.hpp`:
```cpp
static LogicalType InferTypeFromSamples(const std::vector<std::string> &samples,
                                        const XMLSchemaOptions &options,
                                        std::string &winning_datetime_format);
static LogicalType ClassifyDatetimeFormat(const std::string &format);
static void ValidateDatetimeFormatString(const std::string &format);
```

- [ ] **Step 6: Update InferTypeFromSamples call sites**

Search for all calls to `InferTypeFromSamples` (there are ~3-4 call sites). Update each to pass a `winning_datetime_format` output string:

- Where call site has a `XMLColumnInfo` available: store in `column.winning_datetime_format`
- In `InferNestedType` and other places without column info: use a local `std::string` variable. For nested columns, the format is stored in the nested type's column info during the recursive structure building.

- [ ] **Step 7: Write tests for both explicit format and auto-detect**

Add to `test/sql/xml_datetime_format.test`:

```
# Test: Auto-detect should detect ISO dates without explicit format
query I
SELECT typeof(date) FROM read_xml('test/xml/datetime_format_iso.xml') LIMIT 1;
----
DATE

# Test: Auto-detect should detect ISO timestamps
query I
SELECT typeof(timestamp) FROM read_xml('test/xml/datetime_format_iso.xml') LIMIT 1;
----
TIMESTAMP
```

- [ ] **Step 8: Run tests**

Run: `make test`
Expected: Both explicit format and auto-detect tests pass.

- [ ] **Step 9: Commit**

```bash
git add src/xml_schema_inference.cpp src/xml_schema_inference.hpp test/sql/xml_datetime_format.test
git commit -m "feat(#38): replace regex detection with StrpTimeFormat candidate elimination"
```

---

## Task 5: Update ConvertToValue to use winning format

**Files:**
- Modify: `src/xml_schema_inference.cpp:1435-1477` (ConvertToValue)
- Modify: `src/include/xml_schema_inference.hpp:222` (ConvertToValue declaration)

- [ ] **Step 1: Write failing test for US date value parsing**

Add to `test/sql/xml_datetime_format.test`:

```
# Test: US format dates should be parsed to correct date values
query II
SELECT name, date FROM read_xml('test/xml/datetime_format_test.xml', datetime_format='us') ORDER BY name;
----
Conference	2024-12-25
Meeting	2024-03-15
```

- [ ] **Step 2: Run test to verify it fails**

If dates are detected as DATE but ConvertToValue still uses the old YYYY-MM-DD check, US dates will fail to parse correctly.

- [ ] **Step 3: Update ConvertToValue signature and implementation**

Update the signature to accept an optional format string:

```cpp
// In xml_schema_inference.hpp:
static Value ConvertToValue(const std::string &text, const LogicalType &target_type,
                            const std::string &datetime_format = "");
```

Update the implementation in `xml_schema_inference.cpp`:

```cpp
Value XMLSchemaInference::ConvertToValue(const std::string &text, const LogicalType &target_type,
                                         const std::string &datetime_format) {
	if (text.empty()) {
		return Value(); // NULL value
	}

	try {
		switch (target_type.id()) {
		case LogicalTypeId::BOOLEAN: {
			// ... existing boolean logic unchanged ...
		}
		case LogicalTypeId::INTEGER:
			return Value::INTEGER(std::stoi(text));
		case LogicalTypeId::BIGINT:
			return Value::BIGINT(std::stoll(text));
		case LogicalTypeId::DOUBLE:
			return Value::DOUBLE(std::stod(text));
		case LogicalTypeId::DATE: {
			if (!datetime_format.empty()) {
				StrpTimeFormat strp_format;
				StrTimeFormat::ParseFormatSpecifier(datetime_format, strp_format);
				date_t result;
				if (strp_format.TryParseDate(text.c_str(), text.size(), result)) {
					return Value::DATE(result);
				}
				// Format specified but didn't match — this is an error
				throw ConversionException("Could not parse '%s' as DATE with format '%s'", text, datetime_format);
			}
			return Value::DATE(Date::FromString(text));
		}
		case LogicalTypeId::TIMESTAMP: {
			if (!datetime_format.empty()) {
				StrpTimeFormat strp_format;
				StrTimeFormat::ParseFormatSpecifier(datetime_format, strp_format);
				timestamp_t result;
				if (strp_format.TryParseTimestamp(text.c_str(), text.size(), result)) {
					return Value::TIMESTAMP(result);
				}
				throw ConversionException("Could not parse '%s' as TIMESTAMP with format '%s'", text, datetime_format);
			}
			return Value::TIMESTAMP(Timestamp::FromString(text, false));
		}
		case LogicalTypeId::TIMESTAMP_TZ: {
			if (!datetime_format.empty()) {
				StrpTimeFormat strp_format;
				StrTimeFormat::ParseFormatSpecifier(datetime_format, strp_format);
				timestamp_t result;
				if (strp_format.TryParseTimestamp(text.c_str(), text.size(), result)) {
					return Value::TIMESTAMPTZ(result);
				}
				throw ConversionException("Could not parse '%s' as TIMESTAMPTZ with format '%s'", text, datetime_format);
			}
			// Note: fallback must return TIMESTAMPTZ, not TIMESTAMP
			return Value::TIMESTAMPTZ(Timestamp::FromString(text, false));
		}
		case LogicalTypeId::TIME: {
			if (!datetime_format.empty()) {
				StrpTimeFormat strp_format;
				StrTimeFormat::ParseFormatSpecifier(datetime_format, strp_format);
				StrpTimeFormat::ParseResult parse_result;
				if (strp_format.Parse(text.c_str(), text.size(), parse_result)) {
					dtime_t time_result;
					if (parse_result.TryToTime(time_result)) {
						return Value::TIME(time_result);
					}
				}
				throw ConversionException("Could not parse '%s' as TIME with format '%s'", text, datetime_format);
			}
			// Fallback: try DuckDB's built-in time parsing
			return Value(text); // Let DuckDB handle it
		}
		case LogicalTypeId::TIME_TZ: {
			if (!datetime_format.empty()) {
				StrpTimeFormat strp_format;
				StrTimeFormat::ParseFormatSpecifier(datetime_format, strp_format);
				StrpTimeFormat::ParseResult parse_result;
				if (strp_format.Parse(text.c_str(), text.size(), parse_result)) {
					// Construct dtime_tz_t from parsed time and offset
					dtime_t time_result;
					if (parse_result.TryToTime(time_result)) {
						// parse_result.data[7] contains the UTC offset in seconds
						dtime_tz_t tz_result(time_result, parse_result.data[7]);
						return Value::TIMETZ(tz_result);
					}
				}
				throw ConversionException("Could not parse '%s' as TIMETZ with format '%s'", text, datetime_format);
			}
			return Value(text);
		}
		case LogicalTypeId::VARCHAR:
		default:
			return Value(text);
		}
	} catch (const ConversionException &) {
		throw; // Re-throw conversion exceptions (format mismatch errors)
	} catch (...) {
		return Value(text);
	}
}
```

- [ ] **Step 4: Update ConvertToValue call sites to pass datetime_format**

There are 7 call sites for `ConvertToValue`. They fall into two categories:

**Category A — Have column info available** (lines ~1204, ~1315, ~1374):
These are in the main extraction loops where `XMLColumnInfo` is directly available. Update to:
```cpp
value = ConvertToValue(str_value, column.type, column.winning_datetime_format);
```

**Category B — Inside recursive helpers** (lines ~1495, ~1524, ~1533):
These are inside `ExtractValueFromNode` and `ExtractStructFromNode` which work with `LogicalType` but not `XMLColumnInfo`. To thread the format through:
- Add a `const XMLSchemaOptions &options` parameter (or a `const std::unordered_map<std::string, std::string> &column_formats` map) to `ExtractValueFromNode` and `ExtractStructFromNode`.
- Alternatively, store a map of `column_name -> winning_format` in the bind data so it's accessible during extraction. The simplest approach: add a `std::unordered_map<std::string, std::string> winning_datetime_formats` to the bind result (e.g., `XMLReadData` or equivalent), populated during schema inference, and pass it through extraction.
- For these call sites, look up the column name in the map. If not found, pass empty string (existing behavior).

This is the most complex part of the implementation. Look at how `all_varchar` and `ignore_errors` are currently threaded through extraction to follow the same pattern.

- [ ] **Step 5: Run tests**

Run: `make test`
Expected: US date format test passes with correct date values.

- [ ] **Step 6: Commit**

```bash
git add src/xml_schema_inference.cpp src/include/xml_schema_inference.hpp test/sql/xml_datetime_format.test
git commit -m "feat(#38): update ConvertToValue to parse with winning StrpTimeFormat"
```

---

## Task 6: Add error handling and edge case tests

**Files:**
- Modify: `src/xml_schema_inference.cpp` (ConvertToValue error paths)
- Modify: `test/sql/xml_datetime_format.test`

- [ ] **Step 1: Write tests for format mismatch behavior**

When an explicit format doesn't match any sample during inference, the column falls back to VARCHAR (no candidate survives elimination). This is not an error — it's the natural result of candidate elimination.

The error case only occurs when a format wins during inference (samples matched) but a non-sampled row fails during extraction. Since XML files typically sample all rows (`sample_size=50`), this is uncommon. However, ConvertToValue should still handle it correctly.

Add to `test/sql/xml_datetime_format.test`:

```
# Test: Explicit 'us' format with ISO data — no candidate survives, falls back to VARCHAR
query I
SELECT typeof(date) FROM read_xml('test/xml/datetime_format_iso.xml', datetime_format='us') LIMIT 1;
----
VARCHAR

# Test: Mixed formats in a column — no candidate survives elimination, falls back to VARCHAR
query I
SELECT typeof(date) FROM read_xml('test/xml/datetime_format_mixed.xml') LIMIT 1;
----
VARCHAR
```

- [ ] **Step 2: Create mixed-format test data**

Create `test/xml/datetime_format_mixed.xml`:

```xml
<?xml version="1.0"?>
<events>
  <event>
    <name>Meeting</name>
    <date>03/15/2024</date>
  </event>
  <event>
    <name>Conference</name>
    <date>2024-12-25</date>
  </event>
</events>
```

- [ ] **Step 3: Write tests for mixed format behavior**

```
# Test: Mixed formats in a column should fall back to VARCHAR (no format survives elimination)
query I
SELECT typeof(date) FROM read_xml('test/xml/datetime_format_mixed.xml') LIMIT 1;
----
VARCHAR

# Test: Explicit 'us' format with ISO data — format doesn't match any row during inference, stays VARCHAR
query I
SELECT typeof(date) FROM read_xml('test/xml/datetime_format_iso.xml', datetime_format='us') LIMIT 1;
----
VARCHAR
```

- [ ] **Step 4: Add ignore_errors handling in ConvertToValue**

In the ConvertToValue catch block, check if `ignore_errors` is enabled. Since `ignore_errors` is not currently available in ConvertToValue (it's a static method), we need to thread it through. Add it as a parameter:

```cpp
static Value ConvertToValue(const std::string &text, const LogicalType &target_type,
                            const std::string &datetime_format = "",
                            bool ignore_errors = false);
```

When `ignore_errors` is true, catch ConversionExceptions and return NULL instead of re-throwing.

- [ ] **Step 5: Run tests**

Run: `make test`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/xml_schema_inference.cpp src/include/xml_schema_inference.hpp test/sql/xml_datetime_format.test test/xml/datetime_format_mixed.xml
git commit -m "feat(#38): add error handling for datetime format mismatches"
```

---

## Task 7: Add remaining tests

**Files:**
- Modify: `test/sql/xml_datetime_format.test`

- [ ] **Step 1: Add preset tests**

```
# Test: EU preset
query II
SELECT name, date FROM read_xml('test/xml/datetime_format_eu.xml', datetime_format='eu') ORDER BY name;
----
Conference	2024-12-25
Meeting	2024-03-15

# Test: ISO preset (explicit)
query I
SELECT typeof(date) FROM read_xml('test/xml/datetime_format_iso.xml', datetime_format='iso') LIMIT 1;
----
DATE
```

Create `test/xml/datetime_format_eu.xml`:

```xml
<?xml version="1.0"?>
<events>
  <event>
    <name>Meeting</name>
    <date>15/03/2024</date>
  </event>
  <event>
    <name>Conference</name>
    <date>25/12/2024</date>
  </event>
</events>
```

- [ ] **Step 2: Add datetime_format='none' test**

```
# Test: datetime_format='none' disables temporal detection
query I
SELECT typeof(date) FROM read_xml('test/xml/datetime_format_iso.xml', datetime_format='none') LIMIT 1;
----
VARCHAR
```

- [ ] **Step 3: Add all_varchar override test**

```
# Test: all_varchar=true overrides datetime_format
query I
SELECT typeof(date) FROM read_xml('test/xml/datetime_format_test.xml', datetime_format='us', all_varchar=true) LIMIT 1;
----
VARCHAR
```

- [ ] **Step 4: Add list format test**

```
# Test: List of formats
query I
SELECT typeof(date) FROM read_xml('test/xml/datetime_format_test.xml', datetime_format=['%m/%d/%Y', '%Y-%m-%d']) LIMIT 1;
----
DATE
```

- [ ] **Step 5: Add invalid format string test**

```
# Test: Invalid format string should error at bind time
statement error
SELECT * FROM read_xml('test/xml/datetime_format_test.xml', datetime_format='%Q');
----
Invalid datetime_format
```

- [ ] **Step 6: Add read_html test**

Create `test/xml/datetime_format_test.html`:

```html
<html><body><table>
<tr><th>name</th><th>date</th></tr>
<tr><td>Meeting</td><td>03/15/2024</td></tr>
<tr><td>Conference</td><td>12/25/2024</td></tr>
</table></body></html>
```

Add test:

```
# Test: datetime_format works with read_html
query I
SELECT typeof(date) FROM read_html('test/xml/datetime_format_test.html', datetime_format='us') LIMIT 1;
----
DATE
```

- [ ] **Step 7: Add parse_xml and parse_html tests**

```
# Test: datetime_format works with parse_xml
query I
SELECT typeof(date) FROM parse_xml('<events><event><date>03/15/2024</date></event></events>', datetime_format='us');
----
DATE

# Test: datetime_format works with parse_html
query I
SELECT typeof(date) FROM parse_html('<html><body><table><tr><th>date</th></tr><tr><td>03/15/2024</td></tr></table></body></html>', datetime_format='us');
----
DATE
```

- [ ] **Step 8: Run all tests**

Run: `make test`
Expected: All tests pass.

- [ ] **Step 9: Commit**

```bash
git add test/sql/xml_datetime_format.test test/xml/datetime_format_eu.xml
git commit -m "test(#38): add comprehensive datetime_format parameter tests"
```

---

## Task 8: Update documentation

**Files:**
- Modify: `docs/parameters.rst`

- [ ] **Step 1: Read current parameters.rst**

Read the file to understand the existing format and where to add the new parameter.

- [ ] **Step 2: Add datetime_format to parameter table**

Add documentation for the `datetime_format` parameter including:
- Parameter name, type, default value
- Description of behavior
- Preset names table
- Auto-detect priority order note
- Interaction with all_varchar
- Examples

- [ ] **Step 3: Commit**

```bash
git add docs/parameters.rst
git commit -m "docs(#38): document datetime_format parameter"
```

---

## Task 9: Run full test suite and verify no regressions

- [ ] **Step 1: Build clean**

Run: `make release GEN=ninja VCPKG_TOOLCHAIN_PATH=$(pwd)/vcpkg/scripts/buildsystems/vcpkg.cmake`

- [ ] **Step 2: Run full test suite**

Run: `make test`
Expected: All existing tests pass, all new tests pass.

- [ ] **Step 3: Verify existing date detection tests still pass**

Pay special attention to `test/sql/xml_type_inference_order.test` which tests DATE detection. The switch from regex to StrpTimeFormat should maintain backward compatibility for ISO dates.

- [ ] **Step 4: Fix any regressions**

If existing tests fail, investigate whether the StrpTimeFormat auto-detect candidates cover all the patterns the old regex approach matched.

- [ ] **Step 5: Final commit if any fixes were needed**

```bash
git add -u
git commit -m "fix(#38): fix regressions from datetime_format implementation"
```
