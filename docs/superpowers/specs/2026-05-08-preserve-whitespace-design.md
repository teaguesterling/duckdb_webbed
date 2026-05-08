# preserve_whitespace Parameter Design

**Issue:** [#73 — Add option to preserve internal whitespace in text content](https://github.com/teaguesterling/duckdb_webbed/issues/73)
**Date:** 2026-05-08
**Branch:** feature/73-whitespace

## Problem

`read_xml` DOM mode collapses all internal whitespace (newlines, tabs, multi-space runs) in element text content into a single space character via `CleanTextContent()`. This is irreversible and destroys semantically meaningful structure — source code, configuration text, CDATA payloads, and any multi-line content.

The current behavior violates XML 1.0 spec expectations: the spec requires CRLF/CR normalization to LF (§2.11) but does not mandate collapsing internal whitespace. CDATA sections in particular are explicitly intended to preserve literal content.

The SAX reader uses `TrimWhitespace()` which only trims edges (preserving internal whitespace), creating an inconsistency between the two code paths.

## Design

### Parameter

| Name | Type | Default | Description |
|------|------|---------|-------------|
| `preserve_whitespace` | `BOOLEAN` | `true` | Controls whitespace handling in text content |

Default is `true` (breaking change for v2.0.0). Users who need the old collapsing behavior use `preserve_whitespace=false`.

### Behavior Matrix

| Mode | Leading/trailing WS | CRLF / bare CR | Internal WS runs |
|------|---------------------|----------------|-------------------|
| `preserve_whitespace=true` | Trimmed | Normalized to LF | Preserved as-is |
| `preserve_whitespace=false` | Trimmed | Collapsed to space | Collapsed to single space |

Both modes trim leading/trailing ASCII whitespace identically (existing behavior).

EOL normalization (CRLF/CR → LF) applies only when `preserve_whitespace=true`. When `false`, all whitespace runs (including line endings) collapse to a single space as before, making explicit EOL normalization redundant.

### Affected Code Paths

#### 1. `XMLSchemaOptions` (`src/include/xml_schema_inference.hpp`)

Add field:

```cpp
bool preserve_whitespace = true;  // near other boolean flags (~line 64)
```

#### 2. `CleanTextContent()` (`src/xml_schema_inference.cpp:1279-1313`)

Change signature to accept the flag:

```cpp
std::string XMLSchemaInference::CleanTextContent(const std::string &text, bool preserve_whitespace)
```

When `preserve_whitespace=true`:
- Trim leading/trailing ASCII whitespace (existing logic)
- Normalize CRLF (`\r\n`) and bare CR (`\r`) to LF (`\n`)
- Return result without collapsing internal whitespace

When `preserve_whitespace=false`:
- Existing behavior: trim + collapse all whitespace runs to single space

#### 3. DOM Call Sites (8 locations in `xml_schema_inference.cpp`)

Each call site passes `options.preserve_whitespace` (or the equivalent stored boolean):

| Line | Function | Context |
|------|----------|---------|
| 385 | `InferColumnType()` | Text samples for type detection |
| 468 | `InferColumnType()` | LIST instances with attributes |
| 622 | `InferColumnType()` | Cross-record heterogeneous structures |
| 859 | `AnalyzeElement()` | Element pattern analysis |
| 1446 | `ExtractSingleRecord()` | Leaf element extraction |
| 1558 | `ExtractSingleRecordWithSchema()` | Schema-guided extraction |
| 1761 | `ExtractValueFromNode()` | Primitive type extraction |
| 1791 | `ExtractStructFromNode()` | `#text` field from STRUCTs |

Note: The inference call sites (385, 468, 622, 859) affect type detection. When whitespace is preserved, multi-line content is more likely to remain VARCHAR rather than being inferred as a numeric or temporal type. This is correct behavior — content with embedded newlines should not be coerced.

#### 4. SAX Reader (`src/xml_sax_reader.cpp`)

`TrimWhitespace()` (line 105-118) already preserves internal whitespace, which is the correct `preserve_whitespace=true` behavior. Changes needed:

- Add EOL normalization (CRLF/CR → LF) for `preserve_whitespace=true`
- Add whitespace collapsing for `preserve_whitespace=false` to match DOM behavior
- The SAX handler needs access to the `preserve_whitespace` option (via `SAXCallbackContext` or similar)

The function becomes mode-aware, matching `CleanTextContent()`'s dual behavior.

#### 5. Parameter Binding (`src/xml_reader_functions.cpp`)

In `ReadXMLBind()` (~line 1284), add parameter parsing:

```cpp
} else if (kv.first == "preserve_whitespace") {
    schema_options.preserve_whitespace = kv.second.GetValue<bool>();
}
```

Also register as a named parameter in the table function definitions for `read_xml` and `read_html`.

### EOL Normalization Logic

```
Input        → Output
\r\n         → \n     (CRLF → LF)
\r (bare)    → \n     (CR → LF)
\n           → \n     (LF unchanged)
```

This can be implemented as a single pass: scan for `\r`, replace with `\n`, skip next char if it's `\n` (CRLF pair).

### Scope Boundaries

**In scope:**
- `preserve_whitespace` parameter for `read_xml` and `read_html`
- Dual-mode `CleanTextContent()` and `TrimWhitespace()`
- EOL normalization when preserving whitespace
- Tests for both modes, CDATA, mixed content, EOL normalization

**Out of scope:**
- `xml:space="preserve"` attribute handling (per-element control — future enhancement)
- Changes to `read_xml_objects` or other function variants beyond `read_xml`/`read_html`
- Whitespace handling in attribute values (attributes are already handled correctly)

## Testing Strategy

1. **Basic preservation:** Multi-line CDATA content survives round-trip with `preserve_whitespace=true`
2. **Basic collapsing:** Same content collapses with `preserve_whitespace=false`
3. **Default behavior:** Verify default is `true` (no explicit parameter needed)
4. **EOL normalization:** CRLF and bare CR become LF when preserving
5. **DOM/SAX consistency:** Both code paths produce identical output for same input and options
6. **Type inference impact:** Multi-line content stays VARCHAR, not coerced to numeric/temporal
7. **Edge cases:** Empty elements, whitespace-only content, mixed content with nested elements
