# preserve_whitespace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `preserve_whitespace` parameter (default `true`) to `read_xml`/`read_html` that preserves internal whitespace with CRLF/CR→LF normalization instead of collapsing all whitespace to single spaces.

**Architecture:** Dual-mode `CleanTextContent()` function controlled by a boolean flag threaded from `XMLSchemaOptions` through all 8 DOM call sites. SAX path's `TrimWhitespace()` gains matching dual-mode behavior. A shared `NormalizeEOL()` helper handles CRLF/CR→LF normalization for both paths.

**Tech Stack:** C++ (DuckDB extension), libxml2, DuckDB sqllogictest framework

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `src/include/xml_schema_inference.hpp` | Modify | Add `preserve_whitespace` to `XMLSchemaOptions`, update `CleanTextContent` declaration |
| `src/xml_schema_inference.cpp` | Modify | Dual-mode `CleanTextContent()`, update 8 call sites |
| `src/include/xml_sax_reader.hpp` | Modify | Add `preserve_whitespace` field to `SAXCallbackContext` |
| `src/xml_sax_reader.cpp` | Modify | Dual-mode `ProcessTextContent()` replacing `TrimWhitespace()` |
| `src/xml_reader_functions.cpp` | Modify | Bind parameter, thread to SAX context, register on table functions |
| `test/xml/whitespace_cdata.xml` | Create | Test fixture: multi-line CDATA content |
| `test/xml/whitespace_eol.xml` | Create | Test fixture: mixed CRLF/CR/LF line endings |
| `test/sql/github_issue_73_preserve_whitespace.test` | Create | All tests for the feature |

---

### Task 1: Add `preserve_whitespace` to XMLSchemaOptions and update CleanTextContent signature

**Files:**
- Modify: `src/include/xml_schema_inference.hpp:62-66` (add field), `:250` (update declaration)

- [ ] **Step 1: Add the option field**

In `src/include/xml_schema_inference.hpp`, add `preserve_whitespace` after the `streaming` field (line 62):

```cpp
// SAX streaming controls
bool streaming = true; // Enable SAX mode for files exceeding maximum_file_size (default: true)

// Whitespace handling
bool preserve_whitespace = true; // Preserve internal whitespace in text content (default: true for v2.0.0)
```

- [ ] **Step 2: Update CleanTextContent declaration**

In `src/include/xml_schema_inference.hpp`, change the private declaration (line 250) from:

```cpp
static std::string CleanTextContent(const std::string &text);
```

to:

```cpp
static std::string CleanTextContent(const std::string &text, bool preserve_whitespace);
```

- [ ] **Step 3: Verify it compiles**

Run: `blq run build`
Expected: Compile errors at all 8 `CleanTextContent` call sites (missing argument) plus the definition. This confirms we found all call sites.

- [ ] **Step 4: Commit**

```bash
git add src/include/xml_schema_inference.hpp
git commit -m "feat(#73): add preserve_whitespace option to XMLSchemaOptions"
```

---

### Task 2: Implement dual-mode CleanTextContent

**Files:**
- Modify: `src/xml_schema_inference.cpp:1279-1313`

- [ ] **Step 1: Write a test for preserve mode**

Create `test/sql/github_issue_73_preserve_whitespace.test` with the first test — the exact reproduction case from the issue:

```
# name: test/sql/github_issue_73_preserve_whitespace.test
# description: Test preserve_whitespace parameter for read_xml/read_html (GitHub Issue #73)
# group: [sql]

require webbed

# =============================================================================
# Test fixture setup
# =============================================================================

# Create test XML with multi-line CDATA content (the exact reproduction from issue #73)
statement ok
COPY (SELECT '<?xml version="1.0" encoding="UTF-8"?>
<root>
  <code><![CDATA[Let ( [
  a = 1 ;
  b = 2
] ;
  a + b
)]]></code>
</root>' AS xml) TO '__TEST_DIR__/whitespace_cdata.xml' (FORMAT 'csv', HEADER false, QUOTE '');

# =============================================================================
# SECTION 1: Default behavior (preserve_whitespace=true)
# =============================================================================

# Default: newlines are preserved in CDATA content
query I
SELECT length(code) - length(replace(code, chr(10), '')) AS lf_count
FROM read_xml('__TEST_DIR__/whitespace_cdata.xml',
  record_element='root',
  columns={'code': 'VARCHAR'});
----
6
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `blq run test`
Expected: FAIL — currently `CleanTextContent` has wrong arity (missing `bool` argument), won't compile.

- [ ] **Step 3: Implement dual-mode CleanTextContent**

Replace the `CleanTextContent` function body in `src/xml_schema_inference.cpp` (lines 1279-1313). The new implementation:

```cpp
std::string XMLSchemaInference::CleanTextContent(const std::string &text, bool preserve_whitespace) {
	// UTF-8-safe trim: only strip ASCII whitespace.
	// We avoid StringUtil::Trim() because its RTrim has a `ch > 0` guard
	// that treats UTF-8 continuation bytes (negative as signed char) as
	// characters to erase, destroying entire non-ASCII strings (issue #64).
	auto is_ascii_space = [](unsigned char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
	};

	auto lstart = std::find_if_not(text.begin(), text.end(),
	                               [&](char c) { return is_ascii_space(static_cast<unsigned char>(c)); });
	auto rend = std::find_if_not(text.rbegin(), text.rend(), [&](char c) {
		            return is_ascii_space(static_cast<unsigned char>(c));
	            }).base();
	if (lstart >= rend) {
		return "";
	}

	if (preserve_whitespace) {
		// Preserve internal whitespace, normalize CRLF/CR to LF (XML 1.0 §2.11)
		std::string result;
		result.reserve(static_cast<size_t>(rend - lstart));
		for (auto it = lstart; it != rend; ++it) {
			if (*it == '\r') {
				result += '\n';
				// Skip LF after CR (CRLF pair)
				auto next = it + 1;
				if (next != rend && *next == '\n') {
					++it;
				}
			} else {
				result += *it;
			}
		}
		return result;
	}

	// Collapse runs of ASCII whitespace to a single space (original behavior)
	std::string result;
	result.reserve(static_cast<size_t>(rend - lstart));
	bool in_space = false;
	for (auto it = lstart; it != rend; ++it) {
		if (is_ascii_space(static_cast<unsigned char>(*it))) {
			if (!in_space) {
				result += ' ';
				in_space = true;
			}
		} else {
			result += *it;
			in_space = false;
		}
	}
	return result;
}
```

- [ ] **Step 4: Update all 8 call sites to pass `options.preserve_whitespace`**

Each call site in `src/xml_schema_inference.cpp` changes from `CleanTextContent(...)` to `CleanTextContent(..., options.preserve_whitespace)`:

Line 385 (InferColumnType — text samples):
```cpp
std::string text = CleanTextContent((const char *)content, options.preserve_whitespace);
```

Line 468 (InferColumnType — LIST with attributes):
```cpp
std::string text = CleanTextContent((const char *)content, options.preserve_whitespace);
```

Line 622 (InferColumnType — cross-record heterogeneous):
```cpp
std::string text = CleanTextContent((const char *)content, options.preserve_whitespace);
```

Line 859 (AnalyzeElement — pattern analysis):
```cpp
std::string text_content = CleanTextContent((const char *)content, options.preserve_whitespace);
```

Line 1446 (ExtractSingleRecord — leaf element):
```cpp
element_text = CleanTextContent((const char *)text_content, options.preserve_whitespace);
```

Line 1558 (ExtractSingleRecordWithSchema — datetime format):
```cpp
std::string text = CleanTextContent((const char *)text_content, options.preserve_whitespace);
```

Line 1761 (ExtractValueFromNode — primitive types):
```cpp
std::string text = CleanTextContent((const char *)text_content, options.preserve_whitespace);
```

Line 1791 (ExtractStructFromNode — #text field):
```cpp
std::string text = CleanTextContent((const char *)content, options.preserve_whitespace);
```

- [ ] **Step 5: Build and verify compilation**

Run: `blq run build`
Expected: OK — all call sites now have correct arity.

- [ ] **Step 6: Commit**

```bash
git add src/xml_schema_inference.cpp
git commit -m "feat(#73): implement dual-mode CleanTextContent with EOL normalization"
```

---

### Task 3: Bind the parameter and register on table functions

**Files:**
- Modify: `src/xml_reader_functions.cpp:1284` (bind), `:1978-2002` and `:2042-2065` (register)

- [ ] **Step 1: Add parameter binding**

In `src/xml_reader_functions.cpp`, in the `ReadXMLBind()` named parameter loop, add after the `streaming` case (around line 1285):

```cpp
} else if (kv.first == "streaming") {
    schema_options.streaming = kv.second.GetValue<bool>();
} else if (kv.first == "preserve_whitespace") {
    schema_options.preserve_whitespace = kv.second.GetValue<bool>();
} else if (kv.first == "columns") {
```

- [ ] **Step 2: Register on read_xml_single (around line 2002)**

Add after the `streaming` registration:

```cpp
read_xml_single.named_parameters["streaming"] = LogicalType::BOOLEAN;
read_xml_single.named_parameters["preserve_whitespace"] = LogicalType::BOOLEAN;
```

- [ ] **Step 3: Register on read_xml_array (around line 2030)**

Find the `read_xml_array` section and add after `streaming`:

```cpp
read_xml_array.named_parameters["streaming"] = LogicalType::BOOLEAN;
read_xml_array.named_parameters["preserve_whitespace"] = LogicalType::BOOLEAN;
```

- [ ] **Step 4: Register on read_html_single (around line 2065)**

Add after `nullstr`:

```cpp
read_html_single.named_parameters["nullstr"] = LogicalType::ANY;
read_html_single.named_parameters["preserve_whitespace"] = LogicalType::BOOLEAN;
```

- [ ] **Step 5: Register on read_html_array (around line 2094)**

Add after `nullstr`:

```cpp
read_html_array.named_parameters["nullstr"] = LogicalType::ANY;
read_html_array.named_parameters["preserve_whitespace"] = LogicalType::BOOLEAN;
```

- [ ] **Step 6: Build and run the test**

Run: `blq run build && blq run test`
Expected: Build OK. The test from Task 2 should now PASS — the default `preserve_whitespace=true` preserves newlines.

- [ ] **Step 7: Commit**

```bash
git add src/xml_reader_functions.cpp
git commit -m "feat(#73): bind preserve_whitespace parameter for read_xml and read_html"
```

---

### Task 4: Update SAX reader for dual-mode whitespace handling

**Files:**
- Modify: `src/include/xml_sax_reader.hpp:67-73` (add field to context)
- Modify: `src/xml_sax_reader.cpp:105-118` (replace TrimWhitespace), `:282` (call site)
- Modify: `src/xml_reader_functions.cpp:740-745` (thread option to SAX context)

- [ ] **Step 1: Add preserve_whitespace to SAXCallbackContext**

In `src/include/xml_sax_reader.hpp`, add to `SAXCallbackContext` (after line 72):

```cpp
struct SAXCallbackContext {
	SAXRecordAccumulator *accumulator = nullptr;
	idx_t max_rows = 0;                                             // Maximum records to accumulate (0 = unlimited)
	idx_t rows_completed = 0;                                       // Number of completed records so far
	bool stop_parsing = false;                                      // Signal to stop the push parser
	std::vector<SAXRecordAccumulator> *completed_records = nullptr; // Where to store completed records
	bool preserve_whitespace = true;                                // Whitespace handling mode
};
```

- [ ] **Step 2: Replace TrimWhitespace with dual-mode ProcessTextContent**

In `src/xml_sax_reader.cpp`, replace the `TrimWhitespace` function (lines 105-118) with:

```cpp
static std::string ProcessTextContent(const std::string &text, bool preserve_whitespace) {
	auto is_ascii_space = [](unsigned char c) {
		return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
	};
	auto lstart = std::find_if_not(text.begin(), text.end(),
	                               [&](char c) { return is_ascii_space(static_cast<unsigned char>(c)); });
	auto rend = std::find_if_not(text.rbegin(), text.rend(), [&](char c) {
		            return is_ascii_space(static_cast<unsigned char>(c));
	            }).base();
	if (lstart >= rend) {
		return "";
	}

	if (preserve_whitespace) {
		// Preserve internal whitespace, normalize CRLF/CR to LF (XML 1.0 §2.11)
		std::string result;
		result.reserve(static_cast<size_t>(rend - lstart));
		for (auto it = lstart; it != rend; ++it) {
			if (*it == '\r') {
				result += '\n';
				auto next = it + 1;
				if (next != rend && *next == '\n') {
					++it;
				}
			} else {
				result += *it;
			}
		}
		return result;
	}

	// Collapse whitespace (matches DOM CleanTextContent with preserve_whitespace=false)
	std::string result;
	result.reserve(static_cast<size_t>(rend - lstart));
	bool in_space = false;
	for (auto it = lstart; it != rend; ++it) {
		if (is_ascii_space(static_cast<unsigned char>(*it))) {
			if (!in_space) {
				result += ' ';
				in_space = true;
			}
		} else {
			result += *it;
			in_space = false;
		}
	}
	return result;
}
```

- [ ] **Step 3: Update the SAX call site**

In `src/xml_sax_reader.cpp`, line 282, change:

```cpp
value = TrimWhitespace(acc->current_text);
```

to:

```cpp
value = ProcessTextContent(acc->current_text, sax_ctx->preserve_whitespace);
```

- [ ] **Step 4: Thread the option into SAXCallbackContext in xml_reader_functions.cpp**

In `src/xml_reader_functions.cpp`, where the SAX context is initialized (around line 740-745), add the option:

```cpp
gstate.sax_ctx = make_uniq<SAXCallbackContext>();
gstate.sax_ctx->accumulator = gstate.sax_accumulator.get();
gstate.sax_ctx->max_rows = 0;
gstate.sax_ctx->rows_completed = 0;
gstate.sax_ctx->stop_parsing = false;
gstate.sax_ctx->completed_records = &gstate.sax_pending_records;
gstate.sax_ctx->preserve_whitespace = bind_data.schema_options.preserve_whitespace;
```

- [ ] **Step 5: Also thread it in SAXStreamReader::ReadRecords**

In `src/xml_sax_reader.cpp`, in the `ReadRecords` function (around line 386-391), add:

```cpp
SAXCallbackContext ctx;
ctx.accumulator = &accumulator;
ctx.max_rows = max_rows;
ctx.rows_completed = 0;
ctx.stop_parsing = false;
ctx.completed_records = &results;
ctx.preserve_whitespace = options.preserve_whitespace;
```

- [ ] **Step 6: Build and run tests**

Run: `blq run build && blq run test`
Expected: OK — all existing tests pass, Task 2's test still passes.

- [ ] **Step 7: Commit**

```bash
git add src/include/xml_sax_reader.hpp src/xml_sax_reader.cpp src/xml_reader_functions.cpp
git commit -m "feat(#73): add dual-mode whitespace handling to SAX reader"
```

---

### Task 5: Write comprehensive tests

**Files:**
- Modify: `test/sql/github_issue_73_preserve_whitespace.test`
- Modify: `test/sql/read_function_parameters.test`

- [ ] **Step 1: Add remaining tests to the test file**

Append the following sections to `test/sql/github_issue_73_preserve_whitespace.test`:

```
# =============================================================================
# SECTION 2: Explicit preserve_whitespace=false (old collapsing behavior)
# =============================================================================

# Explicit false: newlines are collapsed to spaces
query I
SELECT length(code) - length(replace(code, chr(10), '')) AS lf_count
FROM read_xml('__TEST_DIR__/whitespace_cdata.xml',
  record_element='root',
  columns={'code': 'VARCHAR'},
  preserve_whitespace=false);
----
0

# Explicit false: content is collapsed to single-line
query I
SELECT code
FROM read_xml('__TEST_DIR__/whitespace_cdata.xml',
  record_element='root',
  columns={'code': 'VARCHAR'},
  preserve_whitespace=false);
----
Let ( [ a = 1 ; b = 2 ] ; a + b )

# =============================================================================
# SECTION 3: EOL normalization (CRLF/CR -> LF)
# =============================================================================

# Create test XML with mixed line endings using chr() to inject exact bytes
statement ok
COPY (SELECT '<?xml version="1.0" encoding="UTF-8"?><root><item>' || chr(13) || chr(10) || 'CRLF' || chr(13) || 'CR' || chr(10) || 'LF' || '</item></root>' AS xml) TO '__TEST_DIR__/whitespace_eol.xml' (FORMAT 'csv', HEADER false, QUOTE '');

# Default (preserve): CRLF and CR are normalized to LF
query I
SELECT length(item) - length(replace(item, chr(10), '')) AS lf_count
FROM read_xml('__TEST_DIR__/whitespace_eol.xml',
  record_element='root',
  columns={'item': 'VARCHAR'});
----
3

# Default (preserve): no CR bytes remain
query I
SELECT length(item) - length(replace(item, chr(13), '')) AS cr_count
FROM read_xml('__TEST_DIR__/whitespace_eol.xml',
  record_element='root',
  columns={'item': 'VARCHAR'});
----
0

# =============================================================================
# SECTION 4: DOM/SAX consistency
# =============================================================================

# DOM mode: preserve whitespace (default, small file stays DOM)
query I
SELECT length(code) - length(replace(code, chr(10), '')) AS lf_count
FROM read_xml('__TEST_DIR__/whitespace_cdata.xml',
  record_element='root',
  columns={'code': 'VARCHAR'},
  maximum_file_size=16777216);
----
6

# SAX mode: preserve whitespace (force SAX via maximum_file_size=1)
query I
SELECT length(code) - length(replace(code, chr(10), '')) AS lf_count
FROM read_xml('__TEST_DIR__/whitespace_cdata.xml',
  record_element='root',
  columns={'code': 'VARCHAR'},
  maximum_file_size=1);
----
6

# DOM mode: collapse whitespace
query I
SELECT code
FROM read_xml('__TEST_DIR__/whitespace_cdata.xml',
  record_element='root',
  columns={'code': 'VARCHAR'},
  preserve_whitespace=false,
  maximum_file_size=16777216);
----
Let ( [ a = 1 ; b = 2 ] ; a + b )

# SAX mode: collapse whitespace
query I
SELECT code
FROM read_xml('__TEST_DIR__/whitespace_cdata.xml',
  record_element='root',
  columns={'code': 'VARCHAR'},
  preserve_whitespace=false,
  maximum_file_size=1);
----
Let ( [ a = 1 ; b = 2 ] ; a + b )

# =============================================================================
# SECTION 5: Edge cases
# =============================================================================

# Empty element — both modes return NULL
query I
SELECT item IS NULL FROM read_xml('__TEST_DIR__/whitespace_eol.xml',
  record_element='root',
  columns={'missing': 'VARCHAR'});
----
true

# Whitespace-only content becomes empty string -> NULL after trim
statement ok
COPY (SELECT '<?xml version="1.0" encoding="UTF-8"?><root><item>   </item></root>' AS xml) TO '__TEST_DIR__/whitespace_only.xml' (FORMAT 'csv', HEADER false, QUOTE '');

query I
SELECT item IS NULL FROM read_xml('__TEST_DIR__/whitespace_only.xml',
  record_element='root',
  columns={'item': 'VARCHAR'});
----
true

# =============================================================================
# SECTION 6: read_html also supports preserve_whitespace
# =============================================================================

statement ok
COPY (SELECT '<html><body><table><tr><td>line1
line2
line3</td></tr></table></body></html>' AS html) TO '__TEST_DIR__/whitespace_html.html' (FORMAT 'csv', HEADER false, QUOTE '');

# Default preserve: newlines are kept in HTML text
query I
SELECT length(td) - length(replace(td, chr(10), '')) AS lf_count
FROM read_html('__TEST_DIR__/whitespace_html.html',
  record_element='tr',
  columns={'td': 'VARCHAR'});
----
2

# Explicit false: newlines collapsed
query I
SELECT td
FROM read_html('__TEST_DIR__/whitespace_html.html',
  record_element='tr',
  columns={'td': 'VARCHAR'},
  preserve_whitespace=false);
----
line1 line2 line3
```

- [ ] **Step 2: Add parameter acceptance test**

Append to `test/sql/read_function_parameters.test` in the `read_xml parameter acceptance` section (after the streaming parameter test, around line 71):

```
# Whitespace handling
statement ok
SELECT COUNT(*) FROM read_xml('test/data/employees.xml', preserve_whitespace := true);

statement ok
SELECT COUNT(*) FROM read_xml('test/data/employees.xml', preserve_whitespace := false);
```

Also find the `read_html parameter acceptance` section and add:

```
# Whitespace handling
statement ok
SELECT COUNT(*) FROM read_html('test/data/employees.html', preserve_whitespace := true);

statement ok
SELECT COUNT(*) FROM read_html('test/data/employees.html', preserve_whitespace := false);
```

- [ ] **Step 3: Run all tests**

Run: `blq run test`
Expected: All tests pass, including both new and existing tests.

- [ ] **Step 4: Commit**

```bash
git add test/sql/github_issue_73_preserve_whitespace.test test/sql/read_function_parameters.test
git commit -m "test(#73): add comprehensive tests for preserve_whitespace parameter"
```

---

### Task 6: Update existing tests for new default behavior

The default changed from collapsing whitespace to preserving it. Some existing tests may have expected values that assumed collapsing. This task identifies and updates them.

**Files:**
- Modify: any test files that fail due to the default change

- [ ] **Step 1: Run the full test suite and capture failures**

Run: `blq run test`
Expected: Some existing tests may fail if they had multi-line XML text content whose expected output assumed collapsed whitespace. Capture the list of failures.

- [ ] **Step 2: For each failing test, update the expected value OR add `preserve_whitespace=false`**

For each failure:
- If the test is explicitly testing whitespace collapsing behavior, add `preserve_whitespace=false` to the query.
- If the test was incidentally affected (expected value changes due to preserved newlines), update the expected value to match the new default.
- Do NOT change the `preserve_whitespace` default — the tests adapt to the new default.

- [ ] **Step 3: Run full test suite again**

Run: `blq run test`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add -u test/
git commit -m "test(#73): update existing tests for preserve_whitespace=true default"
```

---

### Task 7: Final verification

- [ ] **Step 1: Run full build from clean state**

Run: `blq run build`
Expected: OK, 0 errors, 0 warnings.

- [ ] **Step 2: Run full test suite**

Run: `blq run test`
Expected: All tests pass.

- [ ] **Step 3: Verify the exact reproduction case from issue #73**

Run this SQL manually (or add as a final sanity check):

```sql
LOAD webbed;
COPY (SELECT '<?xml version="1.0" encoding="UTF-8"?>
<root>
  <code><![CDATA[Let ( [
  a = 1 ;
  b = 2
] ;
  a + b
)]]></code>
</root>' AS xml) TO '/tmp/test_73.xml' (FORMAT 'csv', HEADER false, QUOTE '');

SELECT
  length(code) AS len,
  length(code) - length(replace(code, chr(10), '')) AS lf_count,
  code
FROM read_xml('/tmp/test_73.xml',
  record_element='root',
  columns={'code': 'VARCHAR'});
```

Expected: `lf_count = 6`, `code` contains the original multi-line Let expression with newlines preserved.

- [ ] **Step 4: Commit any final fixes if needed**

If any issues were found in steps 1-3, fix and commit them individually.
