# DuckDB XML Extension - Comprehensive Implementation Plan

## Project Overview

**Objective**: Implement a production-ready XML processing extension for DuckDB that enables SQL-native analysis of XML documents with intelligent schema inference and XPath-based data extraction.

**Repository**: `/mnt/aux-data/teague/Projects/duckdb_xml/`
**Reference Extensions**: 
- `/mnt/aux-data/teague/Projects/duckdb_markdown/` (excellent example)
- `/mnt/aux-data/teague/Projects/duckdb_yaml/` (comprehensive features)
- `/mnt/aux-data/teague/Projects/duckdb_markdown/duckdb/extension/json/` (core patterns)

## Context & Research Foundation

**Completed Analysis**: This project plan is based on comprehensive analysis of existing DuckDB extensions:

1. **Markdown Extension**: Uses cmark-gfm, implements RAII memory management, provides extraction functions returning `LIST<STRUCT>`, supports replacement scan
2. **YAML Extension**: Uses yaml-cpp with vcpkg, provides both `read_yaml()` and `read_yaml_objects()`, extensive parameter system
3. **JSON Extension**: Native DuckDB integration, comprehensive function set, optimal architecture patterns

**Key Architectural Patterns Identified**:
- Custom type with automatic VARCHAR casting
- File reading + object reading function pairs
- Extraction functions returning `LIST<STRUCT>`
- RAII wrappers for C library integration
- Replacement scan for direct file querying
- vcpkg dependency management (preferred over submodules)

## Core Design Decisions

### 1. Dependency Management: vcpkg
**Decision**: Use vcpkg for libxml2 dependency management
**Rationale**: 
- YAML extension successfully uses vcpkg
- Better cross-platform support than git submodules
- Native CMake integration
- Explicit version pinning

### 2. Parsing Library: libxml2
**Decision**: Use libxml2 as the core XML parsing library
**Rationale**:
- Industry standard (browsers, Python lxml)
- Full XPath 1.0/2.0 support
- W3C compliant
- Battle-tested performance
- Rich DOM and SAX APIs

### 3. Schema Inference Strategy
**Decision**: Implement intelligent XML-to-relational flattening
**Core Algorithm**:
1. Always expect single root element (XML requirement)
2. Use root element's children as table rows
3. Analyze 1-2 levels deep for column inference
4. Apply smart type detection (scalar → typed columns, consistent structures → STRUCTs, homogeneous collections → ARRAYs)

### 4. Function Architecture
**Follow established patterns**:
- `read_xml()` - intelligent flattening with schema inference
- `read_xml_objects()` - preserve document structure  
- `xml_extract_*()` - XPath-based extraction functions
- `xml_to_json()` / `xml_valid()` - conversion and utility functions

## Implementation Phases

### Phase 1: Foundation (Weeks 1-3)
**Goal**: Basic extension structure with core XML parsing capabilities

**Tasks**:
1. **Project Setup**
   ```bash
   # Create basic extension structure following markdown extension pattern
   mkdir -p src/include test/sql test/xml docs
   
   # Copy base files from markdown extension and adapt:
   # - CMakeLists.txt
   # - Makefile  
   # - extension_config.cmake
   # - vcpkg.json (with libxml2 dependency)
   ```

2. **Core Extension Structure**
   ```cpp
   // src/xml_extension.cpp - Main entry point
   class XMLExtension : public Extension {
   public:
       void Load(DuckDB &db) override;
       std::string Name() override { return "xml"; }
       std::string Version() const override;
   };
   ```

3. **XML Type System**
   ```cpp
   // src/xml_types.cpp - Custom XML type with VARCHAR casting
   class XMLTypes {
   public:
       static LogicalType XMLType();  // VARCHAR with "xml" alias
       static void Register(DatabaseInstance &db);
   };
   
   // Implement cast functions:
   // - XMLToVarcharCast
   // - VarcharToXMLCast  
   // - XMLToJSONCast (prepare for Phase 3)
   ```

4. **libxml2 RAII Wrapper**
   ```cpp
   // src/include/xml_utils.hpp - Safe memory management
   struct XMLDocRAII {
       xmlDocPtr doc = nullptr;
       xmlXPathContextPtr xpath_ctx = nullptr;
       
       XMLDocRAII(const std::string& xml_str);
       ~XMLDocRAII();
       XMLDocRAII(const XMLDocRAII&) = delete;
       XMLDocRAII& operator=(const XMLDocRAII&) = delete;
   };
   ```

5. **Basic Validation Function**
   ```cpp
   // src/xml_scalar_functions.cpp
   static void XMLValidFunction(DataChunk &args, ExpressionState &state, Vector &result) {
       // Parse XML and return boolean validity
   }
   ```

6. **Basic Tests**
   ```sql
   -- test/sql/xml_basic.test
   require xml
   
   query I
   SELECT xml_valid('<test>valid</test>');
   ----
   true
   
   query I  
   SELECT xml_valid('<invalid><xml>');
   ----
   false
   ```

**Deliverables**:
- Extension loads without errors
- XML type registered with casting
- `xml_valid()` function works
- Basic test suite passes
- Build system configured with vcpkg

### Phase 2: Schema Inference Engine (Weeks 4-6)
**Goal**: Implement intelligent XML document analysis and flattening

**Tasks**:
1. **Schema Analysis Core**
   ```cpp
   // src/xml_schema_inference.cpp
   struct XMLSchemaOptions {
       int32_t schema_depth = 2;
       bool include_attributes = true;
       bool auto_detect_types = true;
       std::string attribute_mode = "columns";  // columns|map|discard
       bool flatten_namespaces = false;
       double array_threshold = 0.8;  // Homogeneity threshold
   };
   
   struct XMLColumnInfo {
       std::string name;
       LogicalType type;
       bool is_attribute;
       std::string xpath;
       double confidence;
   };
   
   class XMLSchemaInference {
   public:
       static std::vector<XMLColumnInfo> InferSchema(
           const std::string& xml_content, 
           const XMLSchemaOptions& options
       );
       
       static LogicalType InferElementType(xmlNodePtr element, const XMLSchemaOptions& options);
   };
   ```

2. **Type Detection System**
   ```cpp
   // Follow YAML extension pattern for type detection
   // Detect: INTEGER, BIGINT, DOUBLE, BOOLEAN, DATE, TIME, TIMESTAMP, VARCHAR
   // Handle: NULL values, empty elements, CDATA sections
   ```

3. **read_xml_objects() Implementation**
   ```cpp
   // src/xml_reader_functions.cpp
   // Simple version: one row per file, filename + XML content
   // Follow read_markdown() pattern from markdown extension
   ```

4. **Basic read_xml() with Schema Inference**
   ```cpp
   // Implement basic flattening for simple XML structures
   // Handle root element extraction
   // Apply schema inference to child elements
   // Generate appropriate column structure
   ```

5. **Parameter System**
   ```cpp
   // Follow YAML extension's comprehensive parameter handling
   // Support: auto_detect, include_attributes, root_element, etc.
   ```

**Test Cases**:
```sql
-- test/sql/xml_schema_inference.test
-- Test basic schema inference
SELECT * FROM read_xml('test/xml/simple.xml');

-- Test attribute handling  
SELECT * FROM read_xml('test/xml/attributes.xml', include_attributes := true);

-- Test document preservation
SELECT filename, content FROM read_xml_objects('test/xml/*.xml');
```

**Test Data**:
```xml
<!-- test/xml/simple.xml -->
<?xml version="1.0"?>
<books>
  <book id="1" available="true">
    <title>Database Systems</title>
    <price>49.99</price>
    <published>2024-01-15</published>
  </book>
  <book id="2" available="false">
    <title>XML Processing</title>
    <price>39.95</price>
    <published>2023-12-01</published>
  </book>
</books>
```

**Deliverables**:
- Schema inference engine working for simple XML
- `read_xml_objects()` function complete
- Basic `read_xml()` with automatic flattening
- Comprehensive parameter system
- Type detection working for common types

### Phase 3: XPath Integration & Extraction Functions (Weeks 7-9)
**Goal**: Implement powerful XPath-based data extraction capabilities

**Tasks**:
1. **XPath Extraction Core**
   ```cpp
   // src/xml_extraction_functions.cpp
   static void XMLExtractElementsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
       auto &xml_vector = args.data[0];
       auto &xpath_vector = args.data[1];
       
       for (idx_t i = 0; i < args.size(); i++) {
           auto xml_str = xml_vector.GetValue(i).ToString();
           auto xpath_str = xpath_vector.GetValue(i).ToString();
           
           auto elements = ExtractByXPath(xml_str, xpath_str);
           
           // Convert to LIST<STRUCT> following markdown extension pattern
           vector<Value> struct_values;
           for (const auto &elem : elements) {
               child_list_t<Value> struct_children;
               struct_children.push_back({"name", Value(elem.name)});
               struct_children.push_back({"text_content", Value(elem.text_content)});
               struct_children.push_back({"attributes", Value::MAP(elem.attributes)});
               struct_children.push_back({"path", Value(elem.path)});
               struct_children.push_back({"line_number", Value::BIGINT(elem.line_number)});
               struct_values.push_back(Value::STRUCT(struct_children));
           }
           
           result.SetValue(i, Value::LIST(struct_values));
       }
   }
   ```

2. **XPath Processing Utilities**
   ```cpp
   // src/xml_utils.cpp
   std::vector<XMLElement> ExtractByXPath(const std::string& xml_str, const std::string& xpath) {
       XMLDocRAII xml_doc(xml_str);
       std::vector<XMLElement> results;
       
       if (!xml_doc.doc || !xml_doc.xpath_ctx) return results;
       
       xmlXPathObjectPtr xpath_obj = xmlXPathEvalExpression(
           BAD_CAST xpath.c_str(), xml_doc.xpath_ctx);
       
       if (xpath_obj && xpath_obj->nodesetval) {
           for (int i = 0; i < xpath_obj->nodesetval->nodeNr; i++) {
               results.push_back(ProcessXMLNode(xpath_obj->nodesetval->nodeTab[i]));
           }
       }
       
       if (xpath_obj) xmlXPathFreeObject(xpath_obj);
       return results;
   }
   ```

3. **Extraction Function Set**
   ```cpp
   // Implement complete extraction function family:
   // xml_extract_elements(xml, xpath) → LIST<STRUCT>
   // xml_extract_text(xml, xpath) → VARCHAR
   // xml_extract_attributes(xml, path) → LIST<STRUCT>
   // xml_extract_cdata(xml) → LIST<STRUCT>
   // xml_extract_comments(xml) → LIST<STRUCT>
   ```

4. **Advanced Schema Inference**
   ```cpp
   // Enhance schema inference to handle:
   // - Nested structures → STRUCT types
   // - Repeated elements → ARRAY types  
   // - Mixed content handling
   // - Namespace-aware processing
   ```

5. **Replacement Scan Support**
   ```cpp
   // src/xml_reader.cpp
   static unique_ptr<TableRef> ReadXMLReplacement(ClientContext &context, const string &table_name, ReplacementScanData *data) {
       if (StringUtil::EndsWith(StringUtil::Lower(table_name), ".xml")) {
           // Return table function call for XML file
       }
       return nullptr;
   }
   
   // Register in xml_extension.cpp:
   config.replacement_scans.emplace_back(XMLReader::ReadXMLReplacement);
   ```

**Test Cases**:
```sql
-- test/sql/xml_xpath.test
-- Test XPath extraction
SELECT elem.name, elem.text_content 
FROM (SELECT UNNEST(xml_extract_elements('<books><book><title>Test</title></book></books>', '//book')) as elem);

-- Test replacement scan
SELECT * FROM 'test/xml/catalog.xml';

-- Test attribute extraction
SELECT attr.name, attr.value
FROM (SELECT UNNEST(xml_extract_attributes('<book id="1" available="true"/>', '//book')) as attr);
```

**Deliverables**:
- Complete XPath extraction function set
- Replacement scan working for `.xml` files
- Advanced schema inference with STRUCT/ARRAY support
- Comprehensive extraction test suite

### Phase 4: Integration & Polish (Weeks 10-12)
**Goal**: Complete the extension with conversion functions, optimization, and documentation

**Tasks**:
1. **XML ↔ JSON Conversion**
   ```cpp
   // src/xml_scalar_functions.cpp
   static void XMLToJSONFunction(DataChunk &args, ExpressionState &state, Vector &result) {
       // Convert XML to JSON using libxml2 + JSON generation
       // Handle: elements → objects, attributes → properties, arrays, etc.
   }
   
   static void ValueToXMLFunction(DataChunk &args, ExpressionState &state, Vector &result) {
       // Convert DuckDB values to XML representation
       // Follow value_to_yaml pattern from YAML extension
   }
   ```

2. **Document Statistics & Analysis**
   ```cpp
   static void XMLStatsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
       // Return STRUCT with:
       // - element_count, attribute_count, text_length
       // - max_depth, namespace_count
       // - has_cdata, has_comments
   }
   
   static void XMLNamespacesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
       // Extract namespace declarations
       // Return LIST<STRUCT(prefix VARCHAR, uri VARCHAR)>
   }
   ```

3. **Performance Optimization**
   ```cpp
   // Optimize for:
   // - Large document processing
   // - Schema inference caching
   // - XPath expression compilation
   // - Memory usage optimization
   ```

4. **Error Handling & Robustness**
   ```cpp
   // Comprehensive error handling:
   // - Malformed XML graceful degradation
   // - XPath syntax error handling
   // - Memory allocation failures
   // - File access errors with ignore_errors parameter
   ```

5. **Documentation & Examples**
   ```markdown
   # docs/EXAMPLES.md - Comprehensive usage examples
   # docs/XPATH_GUIDE.md - XPath syntax guide for XML
   # docs/SCHEMA_INFERENCE.md - Schema inference documentation
   ```

**Comprehensive Test Suite**:
```sql
-- test/sql/xml_conversion.test - XML/JSON conversion
-- test/sql/xml_stats.test - Document analysis functions  
-- test/sql/xml_namespaces.test - Namespace handling
-- test/sql/xml_performance.test - Large document processing
-- test/sql/xml_errors.test - Error handling and edge cases
```

**Test Data Set**:
```xml
<!-- Create comprehensive test XML files -->
<!-- test/xml/complex.xml - Nested structures, namespaces -->
<!-- test/xml/large.xml - Performance testing -->
<!-- test/xml/invalid.xml - Error handling -->
<!-- test/xml/namespaced.xml - XML namespaces -->
<!-- test/xml/mixed_content.xml - Elements with text + children -->
```

**Deliverables**:
- Complete function set implemented
- XML ↔ JSON conversion working
- Document analysis functions  
- Performance optimized for production use
- Comprehensive documentation
- Full test suite with 100+ test cases

## Function Specification

### Core Functions (Must Implement)

```sql
-- File Reading Functions
read_xml(files, [options...]) → table
  -- Parameters: root_element, auto_detect, include_attributes, schema_depth,
  --            attribute_mode, flatten_namespaces, ignore_errors, maximum_file_size

read_xml_objects(files, [options...]) → table(filename VARCHAR, content XML)
  -- Parameters: maximum_file_size, ignore_errors

-- XPath Extraction Functions  
xml_extract_elements(xml_content, xpath) → LIST<STRUCT(
    name VARCHAR, text_content VARCHAR, attributes MAP(VARCHAR, VARCHAR),
    namespace_uri VARCHAR, path VARCHAR, line_number BIGINT
)>

xml_extract_text(xml_content, xpath) → VARCHAR
xml_extract_attributes(xml_content, element_path) → LIST<STRUCT(
    name VARCHAR, value VARCHAR, namespace_uri VARCHAR, element_path VARCHAR
)>

-- Utility Functions
xml_valid(content) → BOOLEAN
xml_well_formed(content) → BOOLEAN  
xml_to_json(xml_content) → JSON
value_to_xml(any_value) → XML

-- Analysis Functions
xml_stats(xml_content) → STRUCT(
    element_count BIGINT, attribute_count BIGINT, text_length BIGINT,
    max_depth INTEGER, namespace_count INTEGER, has_cdata BOOLEAN, has_comments BOOLEAN
)
xml_namespaces(xml_content) → LIST<STRUCT(prefix VARCHAR, uri VARCHAR)>
```

### Optional Functions (Nice to Have)

```sql
xml_extract_cdata(xml_content) → LIST<STRUCT(content VARCHAR, line_number BIGINT)>
xml_extract_comments(xml_content) → LIST<STRUCT(content VARCHAR, line_number BIGINT)>
xml_validate_xsd(xml_content, xsd_schema) → BOOLEAN  -- Future phase
xml_extract_ns(xml_content, xpath, namespace_map) → LIST<STRUCT>  -- Future phase
```

## Technical Requirements

### Dependencies
- **libxml2** >= 2.12.0 (via vcpkg)
- **DuckDB** >= 1.0.0
- **C++17** standard (consistent with DuckDB)

### Platform Support
- **Linux** (primary development)
- **macOS** (must work)
- **Windows** (should work via vcpkg)
- **WebAssembly** (nice to have)

### Performance Targets
- **Schema inference**: < 1 second for typical documents (< 10MB)
- **File processing**: 1,000+ small XML files per second
- **Large documents**: Handle 100MB+ XML files without memory issues
- **Memory efficiency**: Streaming processing for large file sets

### Code Quality Standards
- **Memory safety**: All libxml2 resources managed via RAII
- **Error handling**: Graceful degradation for malformed XML
- **Testing**: 100+ test cases covering edge cases
- **Documentation**: Function documentation and usage examples

## File Structure Template

```
duckdb_xml/
├── CMakeLists.txt                    # Build configuration
├── Makefile                          # Build shortcuts
├── extension_config.cmake            # Extension metadata
├── vcpkg.json                        # Dependency management
├── README.md                         # Project overview
├── XML_EXTENSION_DESIGN.md          # Technical design (existing)
├── XML_EXTENSION_PROJECT_PLAN.md    # This document
├── LICENSE                           # MIT license
├── src/
│   ├── xml_extension.cpp             # Main extension entry point
│   ├── xml_types.cpp                 # XML type system
│   ├── xml_reader.cpp                # Core XML parsing utilities
│   ├── xml_reader_functions.cpp      # Table functions (read_xml, read_xml_objects)
│   ├── xml_extraction_functions.cpp  # Scalar extraction functions
│   ├── xml_scalar_functions.cpp      # Utility and conversion functions
│   ├── xml_schema_inference.cpp      # Schema analysis engine
│   ├── xml_utils.cpp                 # Shared utilities and RAII wrappers
│   └── include/
│       ├── xml_extension.hpp
│       ├── xml_types.hpp
│       ├── xml_reader.hpp
│       ├── xml_extraction_functions.hpp
│       ├── xml_scalar_functions.hpp
│       ├── xml_schema_inference.hpp
│       └── xml_utils.hpp
├── test/
│   ├── sql/
│   │   ├── xml_basic.test            # Basic functionality
│   │   ├── xml_schema_inference.test # Schema inference tests
│   │   ├── xml_extraction.test       # XPath extraction tests  
│   │   ├── xml_conversion.test       # XML/JSON conversion
│   │   ├── xml_stats.test           # Document analysis
│   │   ├── xml_namespaces.test      # Namespace handling
│   │   ├── xml_performance.test     # Performance tests
│   │   └── xml_errors.test          # Error handling
│   └── xml/
│       ├── simple.xml               # Basic test document
│       ├── complex.xml              # Nested structures
│       ├── namespaced.xml           # XML namespaces
│       ├── mixed_content.xml        # Mixed content elements
│       ├── large.xml                # Performance testing
│       └── invalid.xml              # Malformed XML
└── docs/
    ├── EXAMPLES.md                  # Usage examples
    ├── XPATH_GUIDE.md              # XPath reference
    └── SCHEMA_INFERENCE.md         # Schema inference guide
```

## Success Criteria

### Functional Requirements
- [ ] All core functions implemented and working
- [ ] Schema inference accuracy > 90% for well-structured XML
- [ ] XPath support covers common analytical use cases
- [ ] XML ↔ JSON conversion maintains data fidelity
- [ ] Error handling gracefully manages malformed XML
- [ ] Cross-platform compatibility (Linux, macOS, Windows)

### Performance Requirements  
- [ ] Process 1,000+ small XML files per second
- [ ] Handle 100MB+ XML files without memory issues
- [ ] Schema inference completes in < 1 second for typical documents
- [ ] Extension loads in < 100ms (minimal DuckDB startup impact)

### Integration Requirements
- [ ] Follows DuckDB extension architecture patterns
- [ ] Works with DuckDB's replacement scan system
- [ ] Integrates with DuckDB's type system
- [ ] Compatible with DuckDB's vectorized execution
- [ ] Supports DuckDB's parameter binding system

### Quality Requirements
- [ ] Comprehensive test suite (100+ test cases)
- [ ] Memory leak testing passes
- [ ] Thread safety validated
- [ ] Documentation complete with examples
- [ ] Code review ready (following DuckDB standards)

## Implementation Notes

### Critical Success Factors
1. **Follow Existing Patterns**: Study markdown, YAML, and JSON extensions carefully
2. **RAII Everything**: All libxml2 resources must be RAII-managed
3. **Test Early, Test Often**: Build comprehensive tests from day one
4. **Performance Focus**: XML can be large, optimize for real-world usage
5. **Error Handling**: XML can be malformed, handle gracefully

### Common Pitfalls to Avoid
1. **Manual Memory Management**: Use RAII wrappers, never manual `xmlFree()`
2. **Incomplete Error Handling**: XML parsing can fail in many ways
3. **Performance Negligence**: Large XML files can cause memory/performance issues  
4. **Over-Engineering**: Focus on analytical use cases, not general XML processing
5. **Platform Assumptions**: Test across platforms early

### Reference Implementation Strategy
1. **Start with markdown extension** as the primary architectural reference
2. **Use YAML extension** for parameter handling patterns
3. **Study JSON extension** for type system integration
4. **Follow DuckDB conventions** for function naming and behavior

This project plan provides a complete roadmap for implementing a production-ready XML extension that integrates seamlessly with DuckDB while providing powerful XML processing capabilities for analytical workloads.