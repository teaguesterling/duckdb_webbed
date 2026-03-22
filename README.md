[![DuckDB Community Extension](https://img.shields.io/badge/webbed-DuckDB_Community_Extension-blue?logo=duckdb)](https://duckdb.org/community_extensions/extensions/webbed.html)
[![Documentation](https://img.shields.io/badge/docs-readthedocs-blue)](https://duckdb-webbed.readthedocs.io)


# DuckDB XML Extension

A comprehensive XML and HTML processing extension for DuckDB that enables SQL-native analysis of structured documents with intelligent schema inference and powerful XPath-based data extraction.

## Features Overview

### 🔍 **XML & HTML Processing**
- Parse and validate XML/HTML documents
- Extract data using XPath expressions
- Convert between XML, HTML, and JSON formats
- Read files directly into DuckDB tables

### 📊 **Smart Schema Inference** 
- Automatically flatten XML documents into relational tables
- Intelligent type detection (dates, numbers, booleans)
- Configurable element and attribute handling

### 🛠 **Production Ready**
- Built on libxml2 for robust parsing
- Comprehensive error handling
- Memory-safe RAII implementation
- 100% test coverage

---

## Quick Start

```sql
-- Load the extension
LOAD webbed;

-- Read XML files directly
SELECT * FROM 'data.xml';
SELECT * FROM 'config/*.xml';

-- Parse and extract from XML content (returns LIST of all matches)
SELECT xml_extract_text('<book><title>Database Guide</title></book>', '//title');
-- Result: ["Database Guide"]

-- Get single value using list indexing
SELECT xml_extract_text('<book><title>Database Guide</title></book>', '//title')[1];
-- Result: "Database Guide"

-- Parse and extract from HTML content (returns LIST of all matches)
SELECT html_extract_text('<html><body><h1>Welcome</h1></body></html>', '//h1');
-- Result: ["Welcome"]

-- Convert between formats
SELECT xml_to_json('<person><name>John</name><age>30</age></person>');
-- Result: {"person":{"name":{"#text":"John"},"age":{"#text":"30"}}}
```

---

## Function Reference

### 🗂️ **File Reading Functions**

| Function | Description | Example |
|----------|-------------|---------|
| `read_xml(pattern)` | Read XML files into table with schema inference | `SELECT * FROM read_xml('*.xml')` |
| `read_xml_objects(pattern)` | Read XML files as document objects | `SELECT xml FROM read_xml_objects('*.xml')` |
| `read_html(pattern)` | Read HTML files into table | `SELECT * FROM read_html('*.html')` |
| `read_html_objects(pattern)` | Read HTML files as document objects | `SELECT html FROM read_html_objects('*.html')` |

### 🔧 **Content Parsing Functions**

| Function | Description | Example |
|----------|-------------|---------|
| `parse_html(content)` | Parse HTML content string | `SELECT parse_html('<p>Hello</p>')` |
| `xml_valid(content)` | Check if XML is well-formed | `SELECT xml_valid('<root></root>')` |
| `xml_well_formed(content)` | Alias for xml_valid | `SELECT xml_well_formed('<test/>')` |
| `to_xml(value)` | Convert any value to XML | `SELECT to_xml('hello')` |
| `to_xml(value, node_name)` | Convert value to XML with custom node name | `SELECT to_xml('hello', 'greeting')` |
| `xml(value)` | Alias for to_xml | `SELECT xml('hello')` |

### 🎯 **Data Extraction Functions**

| Function | Description | Example |
|----------|-------------|---------|
| `xml_extract_text(xml, xpath)` | Extract all text matches using XPath → LIST(VARCHAR) | `SELECT xml_extract_text(xml, '//title')[1]` |
| `xml_extract_all_text(xml)` | Extract all text content | `SELECT xml_extract_all_text('<p>Hello <b>world</b></p>')` |
| `xml_extract_elements(xml, xpath)` | Extract all elements → LIST(XMLFragment) | `SELECT xml_extract_elements(xml, '//item')[1]` |
| `xml_extract_elements_string(xml, xpath)` | Extract all elements as text (newline-separated) | `SELECT xml_extract_elements_string(xml, '//item')` |
| `xml_extract_attributes(xml, xpath)` | Extract attributes as structs | `SELECT xml_extract_attributes(xml, '//book')` |
| `xml_extract_comments(xml)` | Extract comments with line numbers | `SELECT xml_extract_comments(xml)` |
| `xml_extract_cdata(xml)` | Extract CDATA sections with line numbers | `SELECT xml_extract_cdata(xml)` |

### 🌐 **HTML Extraction Functions**

| Function | Description | Example |
|----------|-------------|---------|
| `html_extract_text(html)` | Extract all text from HTML → VARCHAR | `SELECT html_extract_text(html)` |
| `html_extract_text(html, xpath)` | Extract text from HTML using XPath → LIST(VARCHAR) | `SELECT html_extract_text(html, '//h1')[1]` |
| `html_extract_links(html)` | Extract all links with metadata | `SELECT html_extract_links('<a href="/">Home</a>')` |
| `html_extract_images(html)` | Extract all images with metadata | `SELECT html_extract_images('<img src="pic.jpg" alt="Photo">')` |
| `html_escape(text)` | Escape HTML special characters | `SELECT html_escape('<p>Hello</p>')` |
| `html_unescape(text)` | Decode HTML entities to text | `SELECT html_unescape('&lt;p&gt;Hello&lt;/p&gt;')` |

### 🗂️ **HTML Table Extraction Functions**

| Function | Return Type | Description | Example |
|----------|-------------|-------------|---------|
| `html_extract_tables(html)` | TABLE | Extract tables as rows (table function) | `SELECT * FROM html_extract_tables(html_string)` |
| `html_extract_table_rows(html)` | LIST<STRUCT> | Extract table data as structured rows | `SELECT html_extract_table_rows(html)` |
| `html_extract_tables_json(html)` | LIST<STRUCT> | Extract tables with rich JSON structure | `SELECT html_extract_tables_json(html)` |

### 🔄 **Format Conversion Functions**

| Function | Description | Example |
|----------|-------------|---------|
| `xml_to_json(xml, ...)` | Convert XML to JSON with configurable options | `SELECT xml_to_json('<person><name>John</name></person>')` |
| `json_to_xml(json)` | Convert JSON to XML | `SELECT json_to_xml('{"name":"John"}')` |

**xml_to_json Parameters:**
- `force_list`: Array of element names to always convert to JSON arrays
- `attr_prefix`: Prefix for attributes (default: `'@'`)
- `text_key`: Key for text content (default: `'#text'`)
- `namespaces`: Namespace handling: `'strip'` (default), `'expand'`, `'keep'`
- `xmlns_key`: Key for namespace declarations (default: empty/disabled)
- `empty_elements`: How to handle empty elements: `'object'` (default), `'null'`, `'string'`

**Python xmltodict Compatibility:**

For Python-style xmltodict behavior, create a macro with these defaults:
```sql
CREATE MACRO xmltodict(xml,
                       attr_prefix := '@',
                       text_key := '#',
                       process_namespaces := false,
                       empty_elements := 'object',
                       force_list := []) AS
  xml_to_json(xml,
    attr_prefix := attr_prefix,
    text_key := text_key,
    empty_elements := empty_elements,
    force_list := force_list,
    namespaces := IF(process_namespaces, 'expand', 'strip')
  );

-- Usage matches Python's xmltodict.parse()
SELECT xmltodict('<root><item>Test</item></root>');
-- With namespace processing
SELECT xmltodict('<root xmlns:ns="http://ex.com"><ns:item>Test</ns:item></root>',
                 process_namespaces := true);
```

**HTML Table Extraction Macros:**

Convert HTML tables to JSON or DuckDB tables with type inference:

```sql
-- Extract HTML table as JSON array
CREATE MACRO html_table_to_json(html, idx := 0) AS (
  WITH raw_table AS (
    SELECT row_index, columns
    FROM html_extract_tables(html)
    WHERE table_index = idx
  ),
  headers AS (
    SELECT columns AS h FROM raw_table WHERE row_index = 0
  ),
  data_rows AS (
    SELECT list(columns) AS all_rows, (SELECT h FROM headers) AS header_row
    FROM raw_table
    WHERE row_index > 0
  )
  SELECT to_json(list_transform(
    all_rows,
    row -> map_from_entries(list_zip(header_row, row))
  ))
  FROM data_rows
);

-- Extract HTML table as struct array (requires schema)
CREATE MACRO html_table_to_struct(html, idx, schema) AS TABLE (
  SELECT from_json(html_table_to_json(html, idx), schema) AS result
);

-- Extract HTML table as queryable DuckDB table (requires schema)
CREATE MACRO html_table_to_table(html, idx, schema) AS TABLE (
  SELECT unnest(from_json(html_table_to_json(html, idx), schema), recursive := true)
);

-- Example: Extract specific table from complex HTML document
WITH sample_html AS (
  SELECT '<html>
    <body>
      <h1>Company Report</h1>
      <p>Summary statistics for Q4 2024</p>

      <h2>Employee Directory</h2>
      <table id="employees">
        <tr><th>Name</th><th>Department</th><th>Years</th></tr>
        <tr><td>Alice Smith</td><td>Engineering</td><td>5</td></tr>
        <tr><td>Bob Jones</td><td>Sales</td><td>3</td></tr>
        <tr><td>Carol White</td><td>Engineering</td><td>7</td></tr>
      </table>

      <h2>Sales Performance</h2>
      <table id="sales">
        <tr><th>Product</th><th>Revenue</th><th>Units</th><th>Growth</th></tr>
        <tr><td>Widget A</td><td>50000.00</td><td>1250</td><td>15.5</td></tr>
        <tr><td>Widget B</td><td>75000.00</td><td>2100</td><td>23.2</td></tr>
        <tr><td>Widget C</td><td>32000.00</td><td>800</td><td>-5.1</td></tr>
      </table>

      <h2>Office Locations</h2>
      <table id="offices">
        <tr><th>City</th><th>Country</th><th>Employees</th></tr>
        <tr><td>New York</td><td>USA</td><td>45</td></tr>
        <tr><td>London</td><td>UK</td><td>32</td></tr>
        <tr><td>Tokyo</td><td>Japan</td><td>28</td></tr>
      </table>
    </body>
  </html>' AS html
)

-- Extract first table (index 0) - Employee Directory
SELECT * FROM html_table_to_table(
  (SELECT html FROM sample_html),
  0,
  '[{"Name":"VARCHAR","Department":"VARCHAR","Years":"BIGINT"}]'
);
-- Result: Name (VARCHAR), Department (VARCHAR), Years (BIGINT)

-- Extract second table (index 1) - Sales Performance with type inference
SELECT Product, Revenue, Units, Growth
FROM html_table_to_table(
  (SELECT html FROM sample_html),
  1,
  '[{"Product":"VARCHAR","Revenue":"DOUBLE","Units":"BIGINT","Growth":"DOUBLE"}]'
)
WHERE Growth > 0
ORDER BY Revenue DESC;
-- Result: Revenue (DOUBLE), Units (BIGINT), Growth (DOUBLE) - types inferred from schema!

-- Extract as JSON for further processing
SELECT html_table_to_json((SELECT html FROM sample_html), 2);
-- Result: [{"City":"New York","Country":"USA","Employees":"45"},...]

-- Extract with custom schema (use DECIMAL, INTEGER, FLOAT instead of DOUBLE, BIGINT)
SELECT * FROM html_table_to_struct(
  (SELECT html FROM sample_html),
  1,
  '[{"Product":"VARCHAR","Revenue":"DECIMAL(10,2)","Units":"INTEGER","Growth":"FLOAT"}]'
);

-- Query HTML files with table extraction
SELECT
  h.filename,
  t.Department,
  COUNT(*) as employee_count,
  AVG(t.Years) as avg_years
FROM read_html_objects('reports/*.html') h,
     html_table_to_table(h.html, 0, '[{"Name":"VARCHAR","Department":"VARCHAR","Years":"BIGINT"}]') t
GROUP BY h.filename, t.Department;
```

### 📋 **Analysis & Utility Functions**

| Function | Description | Example |
|----------|-------------|---------|
| `xml_stats(xml)` | Get document statistics | `SELECT xml_stats('<root><item/><item/></root>')` |
| `xml_namespaces(xml)` | List XML namespaces | `SELECT xml_namespaces(xml)` |
| `xml_pretty_print(xml)` | Format XML with indentation | `SELECT xml_pretty_print('<root><item/></root>')` |
| `xml_minify(xml)` | Remove whitespace from XML | `SELECT xml_minify('<root>\n  <item/>\n</root>')` |
| `xml_wrap_fragment(fragment, wrapper)` | Wrap XML fragment with element | `SELECT xml_wrap_fragment('<item/>', 'root')` |
| `xml_validate_schema(xml, xsd)` | Validate against XSD schema | `SELECT xml_validate_schema(xml, schema)` |
| `xml_libxml2_version(name)` | Get libxml2 version info | `SELECT xml_libxml2_version('xml')` |

---

## Usage Examples

### 📖 **Basic XML Processing**

```sql
-- Load and validate XML files
SELECT filename, xml_valid(xml) as is_valid
FROM read_xml_objects('data/*.xml', filename=true);

-- Extract specific data with XPath (use [1] to get single value from LIST)
SELECT
    xml_extract_text(xml, '//book/title')[1] as title,
    xml_extract_text(xml, '//book/author')[1] as author,
    xml_extract_text(xml, '//book/@isbn')[1] as isbn
FROM read_xml_objects('catalog.xml');

-- Convert XML catalog to JSON
SELECT xml_to_json(xml) as json_catalog
FROM read_xml_objects('catalog.xml');
```

### 🌐 **HTML Data Extraction**

```sql
-- Extract all links from HTML pages
SELECT 
    (unnest(html_extract_links(html))).href as url,
    (unnest(html_extract_links(html))).text as link_text
FROM read_html_objects('pages/*.html');

-- Extract table data from HTML
SELECT 
    table_index,
    row_index, 
    columns
FROM html_extract_tables(parse_html('<table><tr><th>Name</th><th>Age</th></tr><tr><td>John</td><td>30</td></tr></table>'));

-- Get page titles and headings (use [1] to get single value from LIST)
SELECT
    html_extract_text(html, '//title')[1] as page_title,
    html_extract_text(html, '//h1')[1] as main_heading
FROM read_html_objects('website/*.html');
```

### 🔍 **Advanced Schema Inference**

```sql
-- Read XML with custom schema options
SELECT * FROM read_xml('products.xml', 
    ignore_errors=true,
    maximum_file_size=1048576,
    filename=true);

-- Analyze document structure before processing
SELECT
    (xml_stats(xml)).element_count,
    (xml_stats(xml)).attribute_count,
    (xml_stats(xml)).text_node_count,
    (xml_stats(xml)).max_depth
FROM read_xml_objects('complex.xml');
```

### 🔄 **Format Conversions**

```sql
-- Convert JSON API response to XML
WITH api_data AS (
    SELECT '{"users":[{"name":"Alice","age":25},{"name":"Bob","age":30}]}' as json_response
)
SELECT json_to_xml(json_response) as xml_format FROM api_data;

-- Basic XML to JSON conversion
SELECT xml_to_json('<person><name>John</name><age>30</age></person>');
-- Result: {"person":{"name":{"#text":"John"},"age":{"#text":"30"}}}

-- Force specific elements to be arrays
SELECT xml_to_json(
    '<catalog><book><title>Book 1</title></book></catalog>',
    force_list := ['title']
);
-- Result: {"catalog":{"book":{"title":[{"#text":"Book 1"}]}}}

-- Custom attribute prefix and text key
SELECT xml_to_json(
    '<item id="123">Product Name</item>',
    attr_prefix := '_',
    text_key := 'value'
);
-- Result: {"item":{"_id":"123","value":"Product Name"}}

-- Namespace handling: strip (default)
SELECT xml_to_json('<root xmlns:ns="http://example.com"><ns:item>Test</ns:item></root>');
-- Result: {"root":{"item":{"#text":"Test"}}}

-- Namespace handling: keep prefixes
SELECT xml_to_json(
    '<root xmlns:ns="http://example.com"><ns:item>Test</ns:item></root>',
    namespaces := 'keep'
);
-- Result: {"root":{"ns:item":{"#text":"Test"}}}

-- Namespace handling: expand to full URIs
SELECT xml_to_json(
    '<root xmlns:ns="http://example.com"><ns:item>Test</ns:item></root>',
    namespaces := 'expand'
);
-- Result: {"root":{"http://example.com:item":{"#text":"Test"}}}

-- Include namespace declarations
SELECT xml_to_json(
    '<root xmlns:ns="http://example.com" xmlns:svg="http://www.w3.org/2000/svg"><ns:item>Test</ns:item></root>',
    namespaces := 'keep',
    xmlns_key := '#xmlns'
);
-- Result: {"root":{"#xmlns":{"ns":"http://example.com","svg":"http://www.w3.org/2000/svg"},"ns:item":{"#text":"Test"}}}

-- Handle empty elements as null
SELECT xml_to_json('<root><item/></root>', empty_elements := 'null');
-- Result: {"root":{"item":null}}

-- Convert XML configuration to JSON for processing
WITH xml_config AS (
    SELECT xml FROM read_xml_objects('config.xml')
)
SELECT json_extract(xml_to_json(xml), '$.config.database.host') as db_host
FROM xml_config;
```

### 🔧 **XML Processing & Utilities**

```sql
-- Minify XML by removing whitespace
SELECT xml_minify('<root>
    <item>
        <name>Product</name>
    </item>
</root>') as minified;
-- Result: <root><item><name>Product</name></item></root>

-- Wrap XML fragments with a root element  
SELECT xml_wrap_fragment('<item>Content</item>', 'wrapper') as wrapped;
-- Result: <wrapper><item>Content</item></wrapper>

-- Extract all matching elements as text
SELECT xml_extract_elements_string(xml, '//book/title') as all_titles
FROM read_xml_objects('library.xml');
-- Result: "Title 1\nTitle 2\nTitle 3"

-- Convert values to XML with custom node names
SELECT to_xml('John Doe', 'author') as xml_author;
-- Result: <author>John Doe</author>

-- Extract comments and CDATA sections
SELECT
    xml_extract_comments(xml) as comments,
    xml_extract_cdata(xml) as cdata_sections
FROM read_xml_objects('document.xml');

-- Get libxml2 version information
SELECT xml_libxml2_version('xml') as version_info;
```

### 📊 **Advanced HTML Table Processing**

```sql
-- Example: Extract product data from an HTML table into a proper DuckDB table

-- Step 1: Use html_extract_tables to get raw table data
WITH raw_table AS (
  SELECT table_index, row_index, columns
  FROM html_extract_tables(
    '<table>
      <tr><th>Product</th><th>Price</th><th>Quantity</th></tr>
      <tr><td>Laptop</td><td>$999.99</td><td>5</td></tr>
      <tr><td>Mouse</td><td>$29.99</td><td>15</td></tr>
      <tr><td>Keyboard</td><td>$79.99</td><td>8</td></tr>
    </table>'
  )
  WHERE table_index = 0  -- First table
)
-- Step 2: Transform into a structured table with proper types
SELECT
  columns[1] AS product_name,
  CAST(regexp_replace(columns[2], '[$,]', '', 'g') AS DECIMAL(10,2)) AS price,
  CAST(columns[3] AS INTEGER) AS quantity,
  CAST(regexp_replace(columns[2], '[$,]', '', 'g') AS DECIMAL(10,2)) *
    CAST(columns[3] AS INTEGER) AS total_value
FROM raw_table
WHERE row_index > 0  -- Skip header row
ORDER BY product_name;

-- Result:
-- ┌──────────────┬────────┬──────────┬─────────────┐
-- │ product_name │  price │ quantity │ total_value │
-- ├──────────────┼────────┼──────────┼─────────────┤
-- │ Keyboard     │  79.99 │        8 │      639.92 │
-- │ Laptop       │ 999.99 │        5 │     4999.95 │
-- │ Mouse        │  29.99 │       15 │      449.85 │
-- └──────────────┴────────┴──────────┴─────────────┘

-- Alternative: Extract tables from HTML files and create a view
CREATE OR REPLACE VIEW product_inventory AS
WITH html_data AS (
  SELECT filename, html FROM read_html_objects('reports/*.html')
),
tables AS (
  SELECT
    filename,
    table_index,
    row_index,
    columns
  FROM html_data, html_extract_tables(html)
  WHERE table_index = 0  -- Assuming product table is first
    AND row_index > 0    -- Skip headers
)
SELECT
  filename,
  columns[1] AS product,
  TRY_CAST(regexp_replace(columns[2], '[^0-9.]', '', 'g') AS DECIMAL(10,2)) AS price,
  TRY_CAST(columns[3] AS INTEGER) AS stock
FROM tables;

-- Now query the view like any DuckDB table
SELECT product, AVG(price) as avg_price, SUM(stock) as total_stock
FROM product_inventory
GROUP BY product
ORDER BY total_stock DESC;

-- Method 2: Using html_extract_table_rows for nested data
SELECT
  filename,
  unnest(html_extract_table_rows(html)) AS table_row
FROM read_html_objects('data/*.html');

-- Method 3: Get rich table metadata with html_extract_tables_json
SELECT
  filename,
  (unnest(html_extract_tables_json(html))).headers AS table_headers,
  (unnest(html_extract_tables_json(html))).row_count AS num_rows
FROM read_html_objects('reports/*.html');
```

---

## Output Formats

### 🔗 **Link Extraction Result**
```sql
html_extract_links(html) → LIST<STRUCT>
```
```json
[
  {
    "text": "External Link",
    "href": "https://example.com", 
    "title": "External Site",
    "line_number": 11
  }
]
```

### 🖼️ **Image Extraction Result**
```sql
html_extract_images(html) → LIST<STRUCT>
```
```json
[
  {
    "alt": "Test Image",
    "src": "image.jpg",
    "title": "A test image", 
    "width": 800,
    "height": 600,
    "line_number": 19
  }
]
```

### 📊 **Table Extraction Result**
```sql
html_extract_tables(html) → TABLE(table_index, row_index, columns)
```
```
┌─────────────┬───────────┬───────────┐
│ table_index │ row_index │  columns  │
├─────────────┼───────────┼───────────┤
│           0 │         0 │ [Name, Age] │
│           0 │         1 │ [John, 25]  │ 
│           0 │         2 │ [Jane, 30]  │
└─────────────┴───────────┴───────────┘
```

### 📈 **XML Statistics Result**
```sql
xml_stats(xml) → STRUCT
```
```json
{
  "element_count": 15,
  "attribute_count": 8, 
  "text_node_count": 12,
  "comment_count": 2,
  "max_depth": 4,
  "namespace_count": 1
}
```

---

## Advanced Features

### 🎛️ **Configuration Options**

All file reading functions support these parameters:

```sql
read_xml('pattern',
    ignore_errors=true,           -- Skip files that can't be parsed
    maximum_file_size=1048576,    -- Max file size in bytes
    filename=true,                -- Include filename column
    columns=['name', 'value'],    -- Specify expected columns
    root_element='root',          -- Specify root element name
    record_element='item',        -- XPath/tag name for elements that become rows
    force_list=['tags'],          -- Column names that should always be LIST type
    auto_detect=true,             -- Auto-detect schema structure
    max_depth=10,                 -- Maximum parsing depth
    unnest_as='struct',           -- How to unnest nested elements
    all_varchar=false,            -- Force all scalar types to VARCHAR (nested preserved)
    nullstr='N/A',                -- String value(s) to interpret as NULL
    attr_mode='prefix',           -- Attribute handling: 'prefix' (default), 'merge', or 'ignore'
    attr_prefix='@',              -- Prefix for attribute columns (default: '@')
    text_key='#text',             -- Key for text content in mixed elements (default: '#text')
    empty_elements='object',      -- How to handle empty elements: 'object' (default), 'null', 'string'
    namespaces='strip',           -- Namespace handling: 'strip' (default), 'expand', 'keep'
    union_by_name=false           -- Combine columns by name when reading multiple files (default: false)
);
```

#### **Parameter Details:**

- **`ignore_errors`**: Continue processing when individual files fail to parse
- **`maximum_file_size`**: Skip files larger than specified bytes (default: 16MB)
- **`filename`**: Add a `filename` column to output with source file path
- **`columns`**: Pre-specify expected column names for better performance
- **`root_element`**: Specify the XML root element for schema inference
- **`record_element`**: XPath expression or tag name identifying which elements become table rows (e.g., `'item'` or `'//item'`)
- **`force_list`**: Element name(s) that should always be inferred as LIST type columns, even if they appear only once (similar to xml_to_json)
- **`auto_detect`**: Enable automatic schema detection and type inference
- **`max_depth`**: Maximum nesting depth to parse (prevents infinite recursion, -1 for unlimited with safety cap at 10)
- **`unnest_as`**: How to handle nested elements ('columns' for flattening, 'struct' for preservation)
- **`all_varchar`**: Force all scalar datatypes to VARCHAR, preserving nested structure. For example, `STRUCT(a INT, b FLOAT, c INT[], d STRUCT(f INT))` becomes `STRUCT(a VARCHAR, b VARCHAR, c VARCHAR[], d STRUCT(f VARCHAR))`. Useful for preventing data loss during type inference or when you want to handle type conversion yourself (default: false)
- **`nullstr`**: String value(s) to interpret as NULL. Accepts a single VARCHAR (e.g., `nullstr='N/A'`) or a list of VARCHAR (e.g., `nullstr=['N/A', '-', 'NULL']`). Matching values are excluded from type inference and converted to NULL during extraction. Matching is case-sensitive. When used with `all_varchar=true`, matched values still become NULL (default: none)
- **`attr_mode`**: How to handle XML attributes: `'prefix'` (default) adds prefix to attribute column names, `'merge'` merges with elements, `'ignore'` ignores attributes
- **`attr_prefix`**: Prefix added to attribute column names when `attr_mode='prefix'` (default: `'@'`)
- **`text_key`**: Key name for text content when elements have mixed content (text + child elements) (default: `'#text'`)
- **`empty_elements`**: How to handle empty elements: `'object'` (default) returns empty struct, `'null'` returns NULL, `'string'` returns empty string
- **`namespaces`**: Namespace handling mode: `'strip'` (default) removes namespace prefixes, `'expand'` replaces prefixes with full URIs, `'keep'` preserves prefixes as-is
- **`union_by_name`**: When reading multiple files, combine columns by name (like DuckDB's `union_by_name` for other formats). Useful when XML files have different schemas (default: false)

### 🔍 **XPath Support**

Full XPath 1.0 expressions are supported. All XPath functions return LIST types (matching PostgreSQL's `xpath()` behavior):

```sql
-- Basic selection (returns LIST of all matches)
xml_extract_text(xml, '//book/title')         -- Returns: ["Title1", "Title2", ...]

-- Get single value using list indexing
xml_extract_text(xml, '//book/title')[1]      -- Returns: "Title1"

-- Attribute selection
xml_extract_text(xml, '//book/@isbn')[1]

-- Conditional selection
xml_extract_text(xml, '//book[@category="fiction"]/title')[1]

-- Position-based selection
xml_extract_text(xml, '//book[1]/title')[1]

-- Text node selection
xml_extract_text(xml, '//book/title/text()')[1]
```

**Namespace Handling in XPath:**

For documents with XML namespaces (e.g., `xmlns="http://example.com"`), use `local-name()` to match elements regardless of namespace:

```sql
-- Won't work on namespaced documents (returns empty list):
xml_extract_text(xml, '//element')              -- Returns: []

-- Works with any namespace:
xml_extract_text(xml, '//*[local-name()="element"]')[1]

-- With predicates:
xml_extract_text(xml, '//*[local-name()="item" and @id="123"]')[1]
```

Note: `read_xml()` automatically strips namespaces during schema inference, so column names won't include namespace prefixes.

### 🏗️ **Schema Inference**

The extension uses a **3-phase deterministic approach** for intelligent schema inference:

1. **Phase 1 - Identify Records**: Determines which XML elements represent table rows
   - Default: Immediate children of root element become rows
   - Custom: Use `record_element` parameter to specify XPath/tag name for row elements

2. **Phase 2 - Identify Columns**: Analyzes immediate children of record elements to determine columns
   - Attributes on record elements become columns
   - Child elements of records become columns
   - Detects when elements repeat within a record (→ LIST type)

3. **Phase 3 - Infer Types**: Determines the appropriate DuckDB type for each column
   - Analyzes sample values for type detection
   - Recursively processes nested structures

**Automatic Type Detection:**

- **Dates**: ISO 8601 formats → DATE type
- **Timestamps**: ISO 8601 with time → TIMESTAMP type
- **Numbers**: Integer and decimal → BIGINT/DOUBLE types
- **Booleans**: true/false, 1/0 → BOOLEAN type
- **Lists**: Repeated elements → LIST type
- **Objects**: Nested elements → STRUCT type

**RSS Feed Example:**

```sql
-- RSS feed with <channel><item>...</item><item>...</item></channel> structure

-- Default behavior: Returns 1 row with the channel and nested items
SELECT * FROM read_xml('feed.xml');

-- Use record_element to extract individual items as rows
SELECT * FROM read_xml('feed.xml', record_element := 'item');
-- Returns 3 rows (one per <item> element)

-- Equivalent XPath syntax
SELECT * FROM read_xml('feed.xml', record_element := '//item');
```

**Understanding record_element vs force_list:**

- **`record_element`**: Identifies which XML elements become **rows** (affects table structure)
- **`force_list`**: Forces specific column names to be **LIST type** (affects column schema)

```sql
-- Example: Product catalog with optional tags
-- XML: <products>
--        <product><name>Widget</name><tag>new</tag></product>
--        <product><name>Gadget</name></product>
--      </products>

-- Without force_list: 'tag' column is VARCHAR (or NULL for Gadget)
SELECT * FROM read_xml('products.xml', record_element := 'product');

-- With force_list: 'tag' column is always LIST<VARCHAR> (even for single tags)
SELECT * FROM read_xml('products.xml',
    record_element := 'product',
    force_list := ['tag']
);
```

**Using all_varchar for Type Safety:**

```sql
-- Example: Employee data with various data types
-- XML: <employees>
--        <employee><id>1</id><age>28</age><salary>75000.50</salary><active>true</active></employee>
--      </employees>

-- Default behavior: Type inference
SELECT * FROM read_xml('employees.xml');
-- Schema: id INTEGER, age INTEGER, salary DOUBLE, active BOOLEAN

-- With all_varchar: All scalars become VARCHAR (prevents data loss during inference)
SELECT * FROM read_xml('employees.xml', all_varchar := true);
-- Schema: id VARCHAR, age VARCHAR, salary VARCHAR, active VARCHAR

-- With nested structures: Structure preserved, scalars become VARCHAR
-- XML: <employee><address><street>123 Main</street><zip>97201</zip></address></employee>
SELECT * FROM read_xml('employees.xml', all_varchar := true);
-- Schema: address STRUCT(street VARCHAR, zip VARCHAR)

-- Cast types explicitly when needed for calculations
SELECT
    id,
    name,
    CAST(age AS INTEGER) as age,
    CAST(salary AS DOUBLE) as salary
FROM read_xml('employees.xml', all_varchar := true)
WHERE CAST(age AS INTEGER) > 30;
```

---

## Error Handling

The extension provides robust error handling:

```sql
-- Graceful error handling with ignore_errors
SELECT * FROM read_xml('*.xml', ignore_errors=true);

-- Validation before processing
SELECT
    filename,
    CASE
        WHEN xml_valid(xml) THEN xml_extract_text(xml, '//title')
        ELSE 'Invalid XML'
    END as title
FROM read_xml_objects('mixed/*.xml', filename=true);

-- Schema validation
SELECT
    docs.filename,
    xml_validate_schema(docs.xml, schema.xml) as is_valid
FROM read_xml_objects('documents/*.xml', filename=true) AS docs
CROSS JOIN read_xml_objects('schema.xsd') AS schema;
```

---

## Performance Tips

### 🚀 **Optimization Strategies**

1. **Use specific XPath expressions** for better performance:
   ```sql
   -- Good: Specific path
   xml_extract_text(xml, '/catalog/book[1]/title')

   -- Slower: Broad search
   xml_extract_text(xml, '//title')
   ```

2. **Filter early** to reduce processing:
   ```sql
   SELECT * FROM read_xml('*.xml')
   WHERE title IS NOT NULL;
   ```

3. **Use read_xml** for structured data, **read_xml_objects** for document analysis:
   ```sql
   -- For data analysis (with schema inference)
   SELECT * FROM read_xml('products.xml');

   -- For document processing (raw content)
   SELECT xml FROM read_xml_objects('products.xml');
   ```

---

## Local development

Clone the repository and the submodules too:
```
$ git clone --recursive https://github.com/teaguesterling/duckdb_webbed
$ cd duckdb_webbed
```

### Using devenv
We have a documented the dependencies with [http://devenv.sh](devenv.sh). When using `devenv` you can be sure that all dependencies for compilation and linting will be available and it will automatically format the files using `clang-format` and `clang-tidy` when you or Claude Code will git commit.

To install devenv for your machine please follow:
https://devenv.sh/getting-started/

### Building the extension and running tests
```bash
# Build extension
make release

# Run tests
make test
```

**Requirements:**
- DuckDB v1.3.2+
- libxml2 (managed via vcpkg)
- C++17 compiler

---

## Technical Details

### 🏗️ **Architecture**
- **Parser**: libxml2 for standards-compliant XML/HTML parsing
- **Memory**: RAII smart pointers for safe resource management  
- **Types**: Custom XML/HTML types with automatic VARCHAR casting
- **XPath**: Full libxml2 XPath engine integration

### 🧪 **Testing**
- 58 comprehensive test suites
- 1901 test assertions passing (100% success rate)
- Real-world test coverage for GitHub issues (#4, #7, #8, #13, #17, #33, #53, #54, #55)
- Cross-platform CI validation
- Memory leak testing with Valgrind
- Complete coverage of all XML/HTML functions

### 📊 **Performance**
- Efficient chunked processing for large files (>2048 rows)
- Lazy evaluation for XPath expressions
- Memory pooling for repeated operations
- Zero-copy string handling where possible

---

## Contributing

Contributions welcome! The extension is production-ready with opportunities for:

- 🔧 Performance optimizations for very large documents
- 🌟 Additional HTML extraction functions (forms, metadata)
- 📈 Advanced XPath 2.0+ features
- 🔄 Enhanced JSON conversion with better type preservation

## License

MIT License - Following DuckDB community extension standards.
