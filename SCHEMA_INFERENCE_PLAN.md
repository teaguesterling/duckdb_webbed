# XML Schema Inference Design

## Problem Statement

Given an XML document, infer a tabular schema where each row represents a record-level element and each column represents data we can extract.

## Core Principle: Deterministic Hierarchy

```
Level 0: Root element (container, not data)
Level 1: Immediate children of root (record elements → rows)
Level 2: Children of record elements (column elements)
Level 3+: Nested structure within columns (STRUCT/LIST types)
```

## Example: RSS Feed

```xml
<rss>                          <!-- Level 0: container -->
  <channel>                    <!-- Level 1: ONE record (one row) -->
    <title>Tech News</title>   <!-- Level 2: column "title" -->
    <link>...</link>           <!-- Level 2: column "link" -->
    <item>                     <!-- Level 2: column "item" (appears 3x) -->
      <title>Article 1</title> <!-- Level 3: nested in item struct -->
      <link>...</link>         <!-- Level 3: nested in item struct -->
    </item>
    <item>...</item>
    <item>...</item>
  </channel>
</rss>
```

**Expected Schema:**
- 1 row (the `<channel>`)
- Columns: `{title: VARCHAR, link: VARCHAR, item: LIST<STRUCT<title VARCHAR, link VARCHAR, ...>>}`

**Key insight**: Channel's `<title>` and item's `<title>` are DIFFERENT and must not be confused.

## Example: Simple Bookstore

```xml
<books>                        <!-- Level 0: container -->
  <book id="1">                <!-- Level 1: record (row 1) -->
    <title>Database</title>    <!-- Level 2: column "title" -->
    <author>John</author>      <!-- Level 2: column "author" -->
  </book>
  <book id="2">                <!-- Level 1: record (row 2) -->
    <title>XML</title>
    <author>Jane</author>
  </book>
</books>
```

**Expected Schema:**
- 2 rows (two `<book>` elements)
- Columns: `{id: VARCHAR, title: VARCHAR, author: VARCHAR}`

**Key insight**: Attributes on record element (`id`) also become columns.

## Algorithm Design

### Phase 1: Identify Record Elements

**Input**: XML document, max_depth setting
**Output**: List of record element nodes (or empty if max_depth = 0)

**Logic**:
```
If max_depth == 0:
  // No introspection, return entire document as XML
  // Column name = root element name
  Return empty record list (special case handled separately)

If force_list specified:
  Use XPath to find elements

Else if root_element specified:
  Find that element, use its children as records

Else (default):
  Root's immediate children are records
```

**Example**:
- RSS: `<rss>` → `<channel>` (1 record)
- Books: `<books>` → `<book>` (2 records)

**Special Case** (max_depth = 0):
- Return schema: `{<root_element_name>: XML}`
  - RSS: `{rss: XML}`
  - Books: `{books: XML}`
- Return data: Single row with entire document as XML value

### Phase 2: Identify Column Elements

**Input**: List of record elements, max_depth setting
**Output**: Map of column names → list of column element nodes

**Logic**:
```
If max_depth == 1:
  // Don't introspect columns, return record as XML
  // Column name = record element name
  Return single column: {<record_element_name>: XML}
    - RSS: {channel: XML}
    - Books: {book: XML}

If max_depth >= 2:
  For each record element:
    1. Extract attributes from record element itself → these become columns
       - Naming based on attr_mode parameter
    2. For each immediate child element:
       - Add to column map: column_name → [list of nodes with that name]
```

**Key**: Do NOT recurse into grandchildren. Only look at immediate children.

**Attribute Handling** (based on `attr_mode`):
- `"columns"`: Attribute name becomes column name directly
- `"prefixed"`: Attribute name prefixed with `attr_prefix` (default `"@"`)
- `"map"`: Collect all into `xml_attributes: MAP<VARCHAR, VARCHAR>`
- `"discard"`: Skip attributes entirely

**Data Structure**:
```cpp
struct ColumnInfo {
    string column_name;
    bool is_attribute;                    // True if from record attribute
    vector<xmlNodePtr> instances;         // All occurrences of this column
    int occurrence_count;                 // Total times this appears
    int records_with_column;              // How many records have this
    bool appears_multiple_times_per_record; // True if any record has 2+ instances
};

map<string, ColumnInfo> columns;
```

**Example (RSS)**:
```
columns = {
  "title": {instances: [channel/title], occurrence_count: 1, ...}
  "link": {instances: [channel/link], occurrence_count: 1, ...}
  "description": {instances: [channel/description], occurrence_count: 1, ...}
  "item": {instances: [channel/item[1], channel/item[2], channel/item[3]],
           occurrence_count: 3,
           appears_multiple_times_per_record: true}
}
```

**Example (Books)**:
```
columns = {
  "id": {is_attribute: true, occurrence_count: 2, ...}
  "title": {instances: [book[1]/title, book[2]/title], occurrence_count: 2, ...}
  "author": {instances: [book[1]/author, book[2]/author], occurrence_count: 2, ...}
}
```

### Phase 3: Infer Column Types

**Input**: ColumnInfo for each column, max_depth setting
**Output**: LogicalType for each column

**Logic**:
```
For each column:
  If is_attribute:
    → VARCHAR (or infer from sample values if type detection enabled)

  Else if all instances are leaf nodes (no children):
    → InferScalarType(sample text values)

  Else if max_depth <= 2:
    // Don't introspect nested structures
    → XML (or VARCHAR with serialized XML)

  Else if appears_multiple_times_per_record:
    // This is a repeated element → LIST type
    → LIST<InferElementType(instances, max_depth - 1)>

  Else:
    // Single complex element per record
    → InferElementType(first instance, max_depth - 1)
```

**Note**: Pass `max_depth - 1` to recursive calls to track depth limit.

**InferElementType(node, remaining_depth)**:
```
If remaining_depth <= 0:
  → XML (or VARCHAR)

If node has only text content:
  → InferScalarType(text)

Else if node has only one type of child repeated:
  // Homogeneous children → LIST
  → LIST<InferElementType(first child, remaining_depth - 1)>

Else if node has multiple different children:
  // Heterogeneous children → STRUCT
  → STRUCT<child1: Type1, child2: Type2, ...>
  // Recursively infer type for each child: InferElementType(child, remaining_depth - 1)

Else:
  → VARCHAR or XML/XMLFragment fallback
```

**Depth Tracking**:
- Start with `max_depth` at record level
- Each level of nesting decrements remaining_depth
- When remaining_depth reaches 0, stop introspection and return XML type

**Example (RSS)**:
- `title`: leaf node → `VARCHAR`
- `link`: leaf node → `VARCHAR`
- `item`: appears 3x per record → `LIST<InferElementType(item)>`
  - item has children: title, link, description, pubDate, guid
  - All different → `STRUCT<title VARCHAR, link VARCHAR, ...>`
  - Final: `LIST<STRUCT<title VARCHAR, link VARCHAR, ...>>`

**Example (Books)**:
- `id`: attribute → `VARCHAR`
- `title`: leaf node → `VARCHAR`
- `author`: leaf node → `VARCHAR`

### Phase 4: Generate Schema

**Input**: Map of column names → types
**Output**: vector<XMLColumnInfo>

**Logic**: Simply convert the column map to the output format

## Key Invariants

1. **Never confuse elements at different levels**: Use parent context to distinguish
2. **Separate "what" from "type"**: First identify columns, then infer types
3. **Analyze in context**: When determining type, only look at instances of that specific column
4. **Predictable behavior**: Same structure always produces same schema

## Implementation Plan

### Step 1: Refactor AnalyzeDocumentStructure

```cpp
struct ColumnAnalysis {
    string name;
    bool is_attribute;
    vector<xmlNodePtr> instances;
    int occurrence_count;
    bool repeats_in_record;  // True if any record has multiple instances
};

vector<ElementPattern> AnalyzeDocumentStructure(xml, options) {
    // Phase 1: Get record elements
    vector<xmlNodePtr> records = IdentifyRecordElements(xml, options);

    // Phase 2: Identify columns (only immediate children)
    map<string, ColumnAnalysis> columns = IdentifyColumns(records);

    // Phase 3: Infer type for each column
    for (auto& [name, column] : columns) {
        LogicalType type = InferColumnType(column, options);
        // Store result...
    }
}
```

### Step 2: Keep AnalyzeElement for Recursive Type Inference

Only call it during type inference for nested structures, never for column identification:

```cpp
LogicalType InferColumnType(ColumnAnalysis column, options) {
    if (column.is_attribute) return VARCHAR;

    if (column.repeats_in_record) {
        // LIST type - analyze element structure
        ElementPattern pattern = AnalyzeElement(column.instances[0], options);
        LogicalType element_type = InferTypeFromPattern(pattern);
        return LIST(element_type);
    }

    // Single element - analyze its structure
    ElementPattern pattern = AnalyzeElement(column.instances[0], options);
    return InferTypeFromPattern(pattern);
}
```

### Step 3: Update Data Extraction

Match the same logic: iterate through record elements, extract each column by name.

## Testing Strategy

1. **RSS Feed**: Should get 1 row with item as LIST<STRUCT>
2. **Simple Books**: Should get 2 rows with scalar columns
3. **Nested Example**: Test STRUCT types
4. **Arrays**: Test LIST types at various levels

## Attribute Handling

Attributes are controlled by the `attr_mode` parameter (aligned with xml_to_json):

**attr_mode = "columns" (default)**:
- Record-level attributes become separate columns: `id`, `available`
- Nested element attributes become prefixed columns: `book_id`, `book_available`

**attr_mode = "prefixed"**:
- All attributes get prefix: `@id`, `@available`, `@book_id`
- Prefix controlled by `attr_prefix` parameter (default: `"@"`)

**attr_mode = "map"**:
- All attributes collected into single MAP column: `xml_attributes: MAP<VARCHAR, VARCHAR>`
- Not yet implemented

**attr_mode = "discard"**:
- Attributes are ignored completely

## Depth Control: max_depth Parameter

The `max_depth` parameter controls how deep into the XML hierarchy we perform schema introspection.

**Depth Levels**:
- **Level 0**: Root element (container)
- **Level 1**: Root's children (records/rows)
- **Level 2**: Grandchildren (columns)
- **Level 3+**: Great-grandchildren (nested structures within columns)

**max_depth Behavior**:

### max_depth = 0
- **No schema introspection**
- Equivalent to `read_xml_objects`
- Returns entire XML document as single row with XML column

**RSS Example**:
```
1 row: {xml: "<rss><channel>...</channel></rss>"}
```

### max_depth = 1
- **Identify records only**
- Each record becomes a row
- Single column containing record's XML

**RSS Example** (1 record):
```
1 row: {xml: "<channel><title>...</title><item>...</item>...</channel>"}
```

**Books Example** (2 records):
```
Row 1: {xml: "<book id='1'><title>Database</title>...</book>"}
Row 2: {xml: "<book id='2'><title>XML</title>...</book>"}
```

### max_depth = 2 (default)
- **Identify records and columns**
- Columns that have children are returned as XML (not introspected)
- Scalar columns are typed (VARCHAR, INTEGER, etc.)

**RSS Example**:
```
1 row: {
  title: "Tech News Feed",
  link: "https://example.com/feed",
  description: "Latest technology news",
  item: "<item><title>First</title>...</item><item>...</item>..."  // XML type
}
```

**Books Example**:
```
Row 1: {id: "1", available: "true", title: "Database", author: "John Smith", price: 49.99}
Row 2: {id: "2", available: "false", title: "XML", author: "Jane Doe", price: 39.95}
```

### max_depth = 3 or -1 (unlimited)
- **Full introspection including nested structures**
- Repeated elements become LIST types
- Complex elements become STRUCT types

**RSS Example**:
```
1 row: {
  title: "Tech News Feed",
  link: "https://example.com/feed",
  item: [
    {title: "First Article", link: "...", description: "...", pubDate: "...", guid: "article-1"},
    {title: "Second Article", link: "...", description: "...", pubDate: "...", guid: "article-2"},
    {title: "Third Article", link: "...", description: "...", pubDate: "...", guid: "article-3"}
  ]
}
```

**Books Example**: (Same as max_depth=2, no deeper nesting)

### max_depth > 3
- Continues introspection to specified depth
- Beyond depth limit, elements become XML type

**Use Cases**:
- `max_depth = 0`: Quick validation, just want raw XML
- `max_depth = 1`: Want record-level XML chunks for manual processing
- `max_depth = 2`: Want flat columns, avoid complex types
- `max_depth = -1`: Full introspection for deeply nested documents

## Edge Cases to Handle

1. Optional columns (appear in some records, not others) → nullable
2. Mixed content (some scalars, some complex) → union types or VARCHAR fallback
3. Empty elements → NULL handling based on `empty_elements` option
4. Attributes vs elements with same name → handled by attr_mode
5. max_depth exceeded → elements become XML type (or VARCHAR with serialized XML)
