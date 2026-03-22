# Task: SAX-based streaming for large XML files — Phase 2 (#17)

## Context

Phase 1 (completed) reduced peak memory by extracting rows lazily from the DOM instead of caching all rows at once. However, the full DOM tree is still built in memory, which is the dominant memory cost for large files. A 100MB XML file produces a ~300-400MB DOM tree.

Phase 2 eliminates the DOM entirely for extraction by using libxml2's SAX (push) parser, which processes XML as a stream of events without building a tree.

## Goal

For files above a configurable threshold, use SAX-based streaming to extract records without ever holding the full DOM in memory. Peak memory should be proportional to a single record, not the full file.

## API

No new user-facing parameters beyond the existing ones. The extension automatically selects SAX mode when appropriate:

```sql
-- Automatically uses SAX for large files (threshold configurable)
SELECT * FROM read_xml('huge_file.xml');

-- User can force SAX mode via parameter
SELECT * FROM read_xml('file.xml', streaming:=true);

-- User can adjust threshold
SELECT * FROM read_xml('file.xml', sax_threshold:=67108864); -- 64MB
```

## Design

### Two-pass approach

1. **Schema inference pass**: Read a small prefix of the file (first N records, configurable via `sample_size`) using SAX to infer the schema. This reuses the existing `InferSchema` logic but feeds it a small buffer instead of the full file.

2. **Extraction pass**: Stream through the full file using SAX callbacks, extracting one record at a time. Each complete record is converted to a `vector<Value>` row and emitted.

### SAX callback architecture

```
xmlCreatePushParserCtxt() → per-chunk xmlParseChunk() → SAX callbacks:
  startElement: push element onto stack, track depth
  endElement:   pop stack; if depth == record depth, emit record
  characters:   accumulate text content for current element
```

State machine per record:
- `SEEKING_RECORD`: skip elements until we find a record element at the right depth
- `IN_RECORD`: accumulate child elements, attributes, and text into a record buffer
- `RECORD_COMPLETE`: convert accumulated data to `vector<Value>`, yield to scan function

### Key data structures

```cpp
struct SAXRecordAccumulator {
    // Current element stack (for tracking depth and parent context)
    std::vector<std::string> element_stack;

    // Current record being accumulated
    std::unordered_map<std::string, std::string> current_values;    // element_name → text
    std::unordered_map<std::string, std::string> current_attrs;     // attr_name → value
    std::unordered_map<std::string, std::vector<std::string>> current_lists; // for repeated elements

    // Record boundary detection
    std::string record_element_name;
    int record_depth = -1;

    // Schema for extraction
    std::vector<std::string> column_names;
    std::vector<LogicalType> column_types;

    // Output buffer
    std::vector<Value> current_row;
    bool row_ready = false;
};
```

### File reading strategy

Read the file in chunks (e.g., 64KB) using `xmlParseChunk()`:

```cpp
xmlParserCtxtPtr ctx = xmlCreatePushParserCtxt(&sax_handler, &accumulator,
                                                nullptr, 0, nullptr);
while (bytes_remaining > 0) {
    auto chunk_size = std::min(bytes_remaining, CHUNK_SIZE);
    file_handle->Read(buffer, chunk_size);
    xmlParseChunk(ctx, buffer, chunk_size, 0);

    // Yield any completed records
    while (accumulator.row_ready) {
        emit(accumulator.current_row);
        accumulator.row_ready = false;
    }
}
xmlParseChunk(ctx, nullptr, 0, 1); // Finalize
xmlFreeParserCtxt(ctx);
```

### Schema inference in SAX mode

For the first pass, use the same SAX parser but only process the first `sample_size` records:

1. Read file in chunks until `sample_size` records have been seen
2. Accumulate record XML fragments into a buffer
3. Feed the buffer to existing `InferSchema()` (which expects a string)

Alternative: implement SAX-native schema inference that builds `ColumnAnalysis` directly from SAX events. This avoids the intermediate string but is more complex.

**Recommendation**: Start with the fragment-accumulation approach. It reuses proven inference code and is simpler to verify. Optimize to SAX-native inference later if profiling shows the first pass is a bottleneck.

### Threshold selection

Default SAX threshold: **64MB** (half of `maximum_file_size`). Below this, Phase 1's lazy DOM extraction is used. Rationale:
- DOM overhead is ~3-4x file size
- For a 64MB file, DOM is ~200-250MB which is manageable
- Above 64MB, DOM memory starts becoming problematic
- SAX has higher per-record overhead due to callback dispatch, so DOM is faster for smaller files

### Limitations and edge cases

- **XPath record_element**: SAX cannot evaluate arbitrary XPath. Support only simple tag-name matching (e.g., `record_element:='item'`). For complex XPath like `//ns:item[@type='active']`, fall back to DOM mode with a warning.
- **Namespace handling**: SAX provides namespace events. For `namespaces='strip'` (default), ignore namespace callbacks. For `namespaces='keep'`, prefix element names.
- **Mixed content**: Elements with both text and child elements need careful handling in the accumulator — track whether we're inside a nested element or at the record level.
- **max_depth**: When depth exceeds `max_depth`, serialize remaining content as XML string. In SAX mode, this means switching to a "raw accumulation" sub-mode that captures events as XML text.

## Implementation steps

### 1. SAXRecordAccumulator class
Create `src/include/xml_sax_reader.hpp` and `src/xml_sax_reader.cpp`:
- SAX callback functions (startElement, endElement, characters, etc.)
- Record accumulation state machine
- Row emission interface

### 2. SAX extraction in ReadDocumentFunction
In `ReadDocumentFunction`, after file size check:
- If `file_size > sax_threshold && !requires_dom_xpath`: use SAX path
- Otherwise: use existing DOM path (Phase 1)

### 3. SAX schema inference
Implement the two-pass approach:
- First pass: SAX-accumulate first N record fragments → feed to InferSchema
- Second pass: SAX-extract records using inferred schema

### 4. Parameters
Add to `XMLSchemaOptions`:
```cpp
bool streaming = false;              // Force SAX mode
idx_t sax_threshold = 67108864;      // 64MB: auto-switch to SAX above this
```

Register `streaming` and `sax_threshold` named parameters in bind functions.

### 5. Tests
- Large file test: generate XML with 100K+ records, verify correct output
- Threshold test: verify DOM vs SAX mode selection
- Edge cases: mixed content, namespaces, max_depth serialization
- Performance test: compare memory usage DOM vs SAX (manual/benchmark)

## Files to create/modify

| File | Change |
|------|--------|
| `src/include/xml_sax_reader.hpp` | New: SAX accumulator and callback declarations |
| `src/xml_sax_reader.cpp` | New: SAX implementation |
| `src/include/xml_schema_inference.hpp` | Add `streaming` and `sax_threshold` to XMLSchemaOptions |
| `src/xml_reader_functions.cpp` | Add SAX path in ReadDocumentFunction, register parameters |
| `src/webbed_extension.cpp` | Include new source file in build |
| `CMakeLists.txt` | Add new source file |

## Verification

1. All existing tests pass (DOM path unchanged for small files)
2. New large-file tests pass with SAX mode
3. Memory profiling shows bounded memory for large files
4. Behavioral equivalence: same output for DOM and SAX on identical input
