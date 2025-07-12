# DuckDB XML Extension Design Document

## Overview

This document outlines the design for a thoughtful XML extension for DuckDB, based on analysis of existing extensions (markdown, YAML, JSON) and XML processing requirements for analytical workloads.

## Executive Summary

**Goal**: Create a SQL-native XML processing extension that enables structured data extraction and analysis from XML documents, following established DuckDB extension patterns.

**Key Design Principles**:
- Focus on **data extraction and querying**, not document transformation
- Use mature, battle-tested XML parsing library (libxml2)
- Follow established extension architecture patterns from JSON/YAML/Markdown
- Provide intelligent schema inference with configurable flattening strategies
- Support XPath for powerful element selection
- Enable cross-format compatibility (XML ↔ JSON conversion)

## Architecture Analysis

### Extension Pattern Comparison

Based on analysis of markdown, YAML, and JSON extensions:

**Common Architecture Components**:
1. **Custom Type System**: Each extension defines a custom type with automatic VARCHAR casting
2. **Function Categories**: File reading, content extraction, type conversion, utility functions  
3. **External Library Integration**: Use robust C/C++ parsing libraries
4. **RAII Memory Management**: Safe handling of C library resources
5. **Replacement Scan Support**: Enable `FROM 'file.ext'` syntax
6. **Structured Return Types**: Use `LIST<STRUCT>` for complex data

**Function Signature Patterns**:
```sql
-- File reading
read_format(files, [parameters...]) → table
read_format_objects(files, [parameters...]) → table  -- preserves structure

-- Content extraction  
format_extract_elements(content) → LIST<STRUCT>

-- Type conversion
format_to_other(content) → other_type
value_to_format(value) → format_type

-- Utilities
format_valid(content) → BOOLEAN
format_stats(content) → STRUCT
```

## Dependency Management Strategy

### Recommendation: vcpkg over git submodules

**Why vcpkg**:
- **Consistent with existing extensions**: YAML extension already uses vcpkg
- **Cross-platform compatibility**: Better Windows/macOS support
- **Version management**: Explicit version pinning and dependency resolution
- **Build integration**: Native CMake integration
- **Security**: Vetted package ecosystem

**vcpkg.json configuration**:
```json
{
  "name": "duckdb-xml",
  "version": "0.1.0",
  "dependencies": [
    {
      "name": "libxml2",
      "version>=": "2.12.0",
      "features": ["tools"]
    }
  ]
}
```

**Alternative**: If vcpkg proves problematic, fall back to git submodules like markdown extension uses for cmark-gfm.

## Smart Schema Inference Design

### Core Schema Strategy

**Intelligent Document Flattening**:

1. **Root Element Handling**: Always expect single root element (XML requirement)
2. **Child Element Analysis**: Analyze 1-2 levels deep for schema inference
3. **Type-aware Column Generation**: 
   - Scalar values → typed columns (INTEGER, VARCHAR, TIMESTAMP, etc.)
   - Consistent child structures → STRUCT columns
   - Heterogeneous collections → LIST columns
   - Complex/inconsistent data → XML-typed columns

### Schema Inference Algorithm

```sql
-- Example XML structure analysis
<?xml version="1.0"?>
<catalog>
  <book id="1" available="true">
    <title>Database Systems</title>
    <author>Author Name</author>
    <price>49.99</price>
    <published>2024-01-15</published>
    <tags>
      <tag>database</tag>
      <tag>sql</tag>
    </tags>
  </book>
  <book id="2" available="false">
    <!-- Similar structure -->
  </book>
</catalog>
```

**Inferred Schema**:
```sql
-- read_xml('catalog.xml') produces:
id INTEGER,           -- from @id attribute
available BOOLEAN,    -- from @available attribute  
title VARCHAR,        -- scalar child element
author VARCHAR,       -- scalar child element
price DECIMAL(10,2),  -- numeric with type detection
published DATE,       -- temporal type detection
tags VARCHAR[],       -- homogeneous collection → array
```

### Schema Configuration Parameters

```sql
read_xml(files, 
  -- Schema inference controls
  root_element := null,                    -- Extract only children of specified root
  auto_detect := true,                     -- Automatic type detection
  schema_depth := 2,                       -- How deep to analyze (1-3)
  
  -- Attribute handling
  include_attributes := true,              -- Include attributes as columns
  attribute_prefix := '',                  -- Prefix for attribute columns (e.g., 'attr_')
  attribute_mode := 'columns',             -- 'columns' | 'map' | 'discard'
  
  -- Content handling  
  text_content_column := 'text_content',   -- Column name for mixed text content
  preserve_mixed_content := false,         -- Handle elements with both text and children
  
  -- Namespace handling
  flatten_namespaces := false,             -- Flatten namespace prefixes
  namespace_aware := true,                 -- Parse with namespace awareness
  
  -- Type detection
  temporal_detection := true,              -- Detect DATE/TIME/TIMESTAMP
  numeric_detection := true,               -- Detect optimal numeric types
  boolean_detection := true,               -- Detect boolean values
  
  -- Collection handling
  array_threshold := 0.8,                  -- Minimum homogeneity for arrays (80%)
  max_array_depth := 3,                    -- Maximum nested array depth
  
  -- Error handling
  ignore_errors := false,                  -- Continue on parsing errors
  maximum_file_size := 16777216            -- 16MB default
)
```

## Core Feature Set

### 1. File Reading Functions

```sql
-- Intelligent schema inference with flattening
read_xml(files, [parameters...]) → table

-- Preserve document structure (one row per file)
read_xml_objects(files, [parameters...]) → table(filename VARCHAR, content XML)
```

### 2. Custom XML Type

```sql
-- Native XML type with automatic VARCHAR casting
CREATE TABLE docs(id INTEGER, config XML);

-- Type conversion with validation
SELECT content::XML FROM read_xml('file.xml');
SELECT xml_content::VARCHAR FROM xml_table;
```

### 3. Content Extraction Functions

```sql
-- XPath-based extraction (primary interface)
xml_extract_elements(xml_content, xpath) → LIST<STRUCT(
    name VARCHAR,
    text_content VARCHAR,
    attributes MAP(VARCHAR, VARCHAR),
    namespace_uri VARCHAR,
    path VARCHAR,
    line_number BIGINT
)>

-- Attribute extraction
xml_extract_attributes(xml_content, element_path) → LIST<STRUCT(
    name VARCHAR,
    value VARCHAR,
    namespace_uri VARCHAR,
    element_path VARCHAR
)>

-- Simple text extraction
xml_extract_text(xml_content, xpath) → VARCHAR
xml_extract_all_text(xml_content) → VARCHAR

-- Specialized extraction
xml_extract_cdata(xml_content) → LIST<STRUCT(content VARCHAR, line_number BIGINT)>
xml_extract_comments(xml_content) → LIST<STRUCT(content VARCHAR, line_number BIGINT)>
```

### 4. Conversion & Utility Functions

```sql
-- Format conversion
xml_to_json(xml_content) → JSON
value_to_xml(any_value) → XML

-- Validation
xml_valid(content) → BOOLEAN
xml_well_formed(content) → BOOLEAN

-- Document analysis
xml_stats(xml_content) → STRUCT(
    element_count BIGINT,
    attribute_count BIGINT,
    text_length BIGINT,
    max_depth INTEGER,
    namespace_count INTEGER,
    has_cdata BOOLEAN,
    has_comments BOOLEAN
)

-- Namespace utilities
xml_namespaces(xml_content) → LIST<STRUCT(prefix VARCHAR, uri VARCHAR)>
```

### 5. Advanced Features (Future)

```sql
-- Schema validation (when XSD support needed)
xml_validate_xsd(xml_content, xsd_schema) → BOOLEAN
xml_validate_dtd(xml_content, dtd_schema) → BOOLEAN

-- Namespace-aware extraction  
xml_extract_ns(xml_content, xpath, namespace_map) → LIST<STRUCT>
```

## Implementation Architecture

### Project Structure
```
duckdb_xml/
├── CMakeLists.txt
├── Makefile  
├── extension_config.cmake
├── vcpkg.json                         # Dependency management
├── README.md
├── XML_EXTENSION_DESIGN.md           # This document
├── src/
│   ├── xml_extension.cpp             # Main extension entry point
│   ├── xml_types.cpp                 # XML type definition & casts
│   ├── xml_reader.cpp                # Core XML parsing utilities
│   ├── xml_reader_functions.cpp      # Table function implementations
│   ├── xml_extraction_functions.cpp  # Scalar extraction functions
│   ├── xml_scalar_functions.cpp      # Conversion & utility functions
│   ├── xml_schema_inference.cpp      # Smart schema analysis
│   ├── xml_copy.cpp                  # COPY TO support (future)
│   └── include/
│       ├── xml_extension.hpp
│       ├── xml_types.hpp
│       ├── xml_reader.hpp
│       ├── xml_extraction_functions.hpp
│       ├── xml_scalar_functions.hpp
│       └── xml_schema_inference.hpp
├── test/
│   ├── sql/
│   │   ├── xml_basic.test
│   │   ├── xml_schema_inference.test
│   │   ├── xml_extraction.test
│   │   ├── xml_xpath.test
│   │   └── xml_conversion.test
│   └── xml/
│       ├── simple.xml
│       ├── complex.xml
│       ├── namespaced.xml
│       ├── mixed_content.xml
│       └── invalid.xml
└── docs/
    ├── EXAMPLES.md
    ├── XPATH_GUIDE.md
    └── SCHEMA_INFERENCE.md
```

### Core Type System
```cpp
// xml_types.hpp
class XMLTypes {
public:
    static LogicalType XMLType();  // VARCHAR with "xml" alias
    static void Register(DatabaseInstance &db);
private:
    static bool XMLToJSONCast(Vector& source, Vector& result, idx_t count, CastParameters& parameters);
    static bool JSONToXMLCast(Vector& source, Vector& result, idx_t count, CastParameters& parameters);
    static bool VarcharToXMLCast(Vector& source, Vector& result, idx_t count, CastParameters& parameters);
};
```

### RAII Memory Management
```cpp
// xml_reader.hpp - Safe libxml2 wrapper
struct XMLDocRAII {
    xmlDocPtr doc = nullptr;
    xmlXPathContextPtr xpath_ctx = nullptr;
    
    XMLDocRAII(const std::string& xml_str) {
        doc = xmlParseMemory(xml_str.c_str(), xml_str.length());
        if (doc) {
            xpath_ctx = xmlXPathNewContext(doc);
        }
    }
    
    ~XMLDocRAII() {
        if (xpath_ctx) xmlXPathFreeContext(xpath_ctx);
        if (doc) xmlFreeDoc(doc);
    }
    
    // Delete copy operations for safety
    XMLDocRAII(const XMLDocRAII&) = delete;
    XMLDocRAII& operator=(const XMLDocRAII&) = delete;
};
```

### Schema Inference Engine
```cpp
// xml_schema_inference.hpp
struct XMLSchemaOptions {
    int32_t schema_depth = 2;
    bool include_attributes = true;
    bool auto_detect_types = true;
    std::string attribute_mode = "columns";  // columns|map|discard
    bool flatten_namespaces = false;
    // ... other options
};

struct XMLColumnInfo {
    std::string name;
    LogicalType type;
    bool is_attribute;
    std::string xpath;
    double confidence;  // Schema inference confidence (0.0-1.0)
};

class XMLSchemaInference {
public:
    static std::vector<XMLColumnInfo> InferSchema(
        const std::string& xml_content, 
        const XMLSchemaOptions& options
    );
    
    static LogicalType InferElementType(
        xmlNodePtr element, 
        const XMLSchemaOptions& options
    );
};
```

## Library Selection: libxml2

### Why libxml2?

**Advantages**:
- **Industry Standard**: Used by browsers, Python lxml, major XML tools
- **Complete Feature Set**: Full W3C compliance, XPath 1.0/2.0 support
- **Battle-tested**: Mature, stable, high-performance
- **Cross-platform**: Excellent compatibility across all target platforms
- **Rich API**: Comprehensive DOM and SAX parsing capabilities
- **Memory Efficient**: Optimized for large document processing

**Integration Strategy**:
- Use vcpkg for dependency management
- Implement RAII wrappers throughout for memory safety
- Leverage XPath for powerful element selection
- Start with DOM parsing, add streaming (SAX) support later if needed

### Alternative Libraries Considered

**pugixml**: Lighter weight but limited XPath support
**TinyXML-2**: Too basic, no XPath
**Xerces-C++**: Overkill, complex API

## Example Usage Patterns

### Basic XML File Processing
```sql
-- Load extension
LOAD xml;

-- Direct file querying (replacement scan)
SELECT * FROM 'catalog.xml';
SELECT * FROM 'data/*.xml' WHERE available = true;

-- Explicit file reading with schema inference
SELECT * FROM read_xml('books.xml', 
    root_element := 'catalog',
    include_attributes := true,
    auto_detect := true
);

-- Preserve document structure
SELECT filename, content FROM read_xml_objects('configs/*.xml');
```

### XPath-based Data Extraction
```sql
-- Extract specific elements using XPath
SELECT 
    filename,
    elem.name,
    elem.text_content,
    elem.attributes['isbn'] AS isbn
FROM read_xml_objects('catalog.xml') books,
     UNNEST(xml_extract_elements(books.content, '//book[@available="true"]')) AS elem;

-- Complex XPath queries
SELECT xml_extract_text(content, '//book[price < 50]/title[1]') AS affordable_books
FROM read_xml_objects('catalog.xml');

-- Extract all attributes from specific elements
SELECT attr.name, attr.value
FROM read_xml_objects('config.xml') configs,
     UNNEST(xml_extract_attributes(configs.content, '//database')) AS attr;
```

### Cross-format Integration
```sql
-- Convert XML to JSON for complex analysis
WITH xml_as_json AS (
    SELECT xml_to_json(content) AS json_data
    FROM read_xml_objects('complex.xml')
)
SELECT json_extract_string(json_data, '$.catalog.books[0].title') AS first_title
FROM xml_as_json;

-- Join XML data with relational tables
SELECT 
    books.title,
    authors.biography
FROM read_xml('catalog.xml') books
JOIN authors ON books.author_id = authors.id;
```

### Schema Analysis and Validation
```sql
-- Analyze document structure
SELECT 
    filename,
    stats.element_count,
    stats.max_depth,
    stats.namespace_count
FROM read_xml_objects('documents/*.xml') docs,
     LATERAL xml_stats(docs.content) AS stats;

-- Validate XML documents
SELECT filename, xml_valid(content) AS is_valid
FROM read_xml_objects('data/*.xml')
WHERE NOT xml_valid(content);

-- Extract namespace information
SELECT ns.prefix, ns.uri
FROM read_xml_objects('namespaced.xml') docs,
     UNNEST(xml_namespaces(docs.content)) AS ns;
```

### Advanced Schema Configuration
```sql
-- Custom schema handling for complex documents
SELECT * FROM read_xml('mixed_content.xml',
    root_element := 'document',
    schema_depth := 3,
    attribute_mode := 'map',
    preserve_mixed_content := true,
    array_threshold := 0.9
);

-- Handle namespaced documents
SELECT * FROM read_xml('soap_response.xml',
    namespace_aware := true,
    flatten_namespaces := true,
    root_element := 'soap:Envelope'
);
```

## Deliberately Excluded Features

### Complexity Management

**Excluded for Simplicity**:
1. **XSLT Transformations**: Too complex, use external tools
2. **XQuery Support**: Massive implementation effort, limited SQL integration value
3. **DTD/Schema Generation**: Academic feature, low practical analytical value
4. **XML Modification Functions**: Complex state management, not analytical focus
5. **Custom Entity Resolution**: Security and complexity concerns
6. **XML Digital Signatures**: Security feature outside analytical scope

**Rationale**: Focus on the 80% use case of extracting structured data from XML for analysis, not general-purpose XML document processing.

## Implementation Phases

### Phase 1: Core Foundation (Weeks 1-3)
- [ ] Basic extension structure and build system
- [ ] XML type definition with VARCHAR casting
- [ ] libxml2 integration with RAII wrappers
- [ ] Basic `xml_valid()` and `xml_extract_text()` functions
- [ ] Simple test suite

### Phase 2: File Reading (Weeks 4-6)  
- [ ] `read_xml_objects()` implementation
- [ ] Basic schema inference engine
- [ ] `read_xml()` with automatic flattening
- [ ] Replacement scan support for `.xml` files
- [ ] Comprehensive parameter handling

### Phase 3: Extraction Functions (Weeks 7-9)
- [ ] XPath-based `xml_extract_elements()`
- [ ] Attribute extraction functions
- [ ] CDATA and comment extraction
- [ ] Type detection and conversion utilities
- [ ] Advanced schema inference options

### Phase 4: Integration & Polish (Weeks 10-12)
- [ ] XML ↔ JSON conversion functions
- [ ] Document statistics and analysis
- [ ] Namespace handling utilities
- [ ] Performance optimization
- [ ] Documentation and examples

### Phase 5: Advanced Features (Future)
- [ ] Schema validation (XSD/DTD)
- [ ] Streaming parser for very large files
- [ ] COPY TO XML support
- [ ] Advanced XPath 2.0 features

## Performance Considerations

**Optimization Strategies**:
- **Lazy Evaluation**: Parse only when needed
- **Schema Caching**: Cache inferred schemas for repeated file patterns
- **Vectorized Processing**: Process multiple documents in batches
- **Memory Management**: Use libxml2's efficient memory allocators
- **XPath Compilation**: Cache compiled XPath expressions

**Expected Performance**:
- Target: 1,000+ XML documents/second for schema inference
- Memory: Stream processing for files > 100MB
- Parallelization: Thread-safe for concurrent query execution

## Risk Mitigation

### Technical Risks

**Dependency Management**:
- Primary: Use vcpkg for libxml2
- Fallback: Git submodules if vcpkg proves problematic
- Testing: Validate across Linux, macOS, Windows, WASM

**Memory Safety**:
- RAII wrappers for all libxml2 resources
- Comprehensive error handling
- Memory leak testing in CI/CD

**Performance**:
- Start with DOM parsing (simpler, safer)
- Add streaming SAX parser if needed for very large files
- Benchmark against real-world XML datasets

### Scope Risks

**Feature Creep Prevention**:
- Resist XSLT/XQuery functionality requests
- Focus on data extraction, not document transformation
- Maintain analytical workload focus

**Standards Compliance**:
- Support XML 1.0 + Namespaces (core use cases)
- Skip XML 1.1 edge cases unless specifically needed
- Prioritize real-world compatibility over theoretical completeness

## Success Metrics

**Functional Goals**:
- [ ] Handle 95% of common XML document structures
- [ ] Schema inference accuracy > 90% for well-structured documents
- [ ] XPath support covers most analytical use cases
- [ ] Seamless integration with existing DuckDB workflows

**Performance Goals**:
- [ ] Process 1,000+ small XML files per second
- [ ] Handle 100MB+ XML files without memory issues
- [ ] Schema inference completes in < 1 second for typical documents
- [ ] Minimal impact on DuckDB startup time

**Integration Goals**:
- [ ] Works across all DuckDB target platforms
- [ ] Consistent with JSON/YAML extension patterns
- [ ] Easy installation via community extensions
- [ ] Comprehensive documentation and examples

## Conclusion

This XML extension design balances power and simplicity by focusing on analytical workloads rather than general-purpose XML processing. By following established DuckDB extension patterns and leveraging mature libraries like libxml2, we can deliver a robust, performant XML processing capability that integrates seamlessly with SQL workflows.

The intelligent schema inference system addresses the key challenge of XML's flexibility while maintaining the structured data benefits that make DuckDB valuable for analytics. The XPath integration provides powerful data extraction capabilities while keeping the implementation manageable.

Key differentiators:
- **SQL-native XML processing** for analytical workflows
- **Intelligent schema inference** with configurable flattening
- **XPath integration** for powerful element selection  
- **Cross-format compatibility** (XML ↔ JSON)
- **Proven architecture patterns** from successful extensions

This foundation provides a solid base for XML analytics in DuckDB while maintaining the performance and simplicity characteristics that make DuckDB extensions successful.