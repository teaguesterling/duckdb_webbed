# DuckDB XML Extension

A thoughtful XML processing extension for DuckDB that enables SQL-native analysis of XML documents with intelligent schema inference and powerful XPath-based data extraction.

## Status: Core Implementation Complete

This extension has implemented the core functionality for XML processing in DuckDB. See [XML_EXTENSION_DESIGN.md](XML_EXTENSION_DESIGN.md) for the comprehensive technical design.

## Vision

Enable seamless XML data processing in DuckDB with:

- **Intelligent Schema Inference**: Automatically flatten XML documents into relational tables
- **XPath Integration**: Powerful element selection and data extraction
- **Cross-format Compatibility**: Seamless XML ↔ JSON conversion
- **SQL-native Processing**: Query XML files directly using `FROM 'file.xml'` syntax

## Implemented Features

### Core Functions
```sql
-- File reading with automatic schema inference
read_xml(files, [options...]) → table                    -- ✅ IMPLEMENTED
read_xml_objects(files, [options...]) → table            -- ✅ IMPLEMENTED

-- XPath-based extraction  
xml_extract_text(xml_content, xpath) → VARCHAR           -- ✅ IMPLEMENTED
xml_extract_all_text(xml_content) → VARCHAR              -- ✅ IMPLEMENTED
xml_extract_elements(xml_content, xpath) → LIST<STRUCT>  -- ✅ IMPLEMENTED
xml_extract_attributes(xml_content, xpath) → LIST<STRUCT> -- ✅ IMPLEMENTED

-- Validation and utility functions
xml_valid(content) → BOOLEAN                             -- ✅ IMPLEMENTED
xml_well_formed(content) → BOOLEAN                       -- ✅ IMPLEMENTED
xml_validate_schema(xml_content, xsd_schema) → BOOLEAN   -- ✅ IMPLEMENTED

-- Format conversion functions
xml_to_json(xml_content) → VARCHAR                       -- ✅ IMPLEMENTED
json_to_xml(json_content) → VARCHAR                      -- ✅ IMPLEMENTED

-- Document analysis and formatting
xml_stats(xml_content) → STRUCT                          -- ✅ IMPLEMENTED
xml_namespaces(xml_content) → LIST<STRUCT>               -- ✅ IMPLEMENTED
xml_pretty_print(xml_content) → VARCHAR                  -- ✅ IMPLEMENTED
xml_minify(xml_content) → VARCHAR                        -- ⚠️ PARTIAL (see known issues)

-- Content extraction (specialized)
xml_extract_comments(xml_content) → LIST<STRUCT>         -- ⚠️ KNOWN ISSUE
xml_extract_cdata(xml_content) → LIST<STRUCT>            -- ⚠️ KNOWN ISSUE

-- Legacy compatibility
xml_libxml2_version() → VARCHAR                          -- ✅ IMPLEMENTED
```

### Example Usage
```sql
-- Load extension
LOAD xml;

-- Query XML files directly with replacement scan
SELECT * FROM 'catalog.xml';
SELECT * FROM 'data/*.xml' WHERE available = true;

-- Basic XML validation and text extraction
SELECT xml_valid('<root><item>test</item></root>');
SELECT xml_extract_text('<books><book>Title</book></books>', '//book');

-- Format conversion between XML and JSON
SELECT xml_to_json('<catalog><book id="1"><title>Database Systems</title></book></catalog>');
SELECT json_to_xml('{"root":{"name":"test","value":"123"}}');

-- Document analysis and statistics
SELECT xml_stats('<catalog><book id="1"><title>DB</title></book></catalog>');
SELECT xml_namespaces('<root xmlns:book="http://example.com/book"><book:item/></root>');

-- XSD schema validation
SELECT xml_validate_schema('<root><item>test</item></root>', 
    '<?xml version="1.0"?><xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">
     <xs:element name="root"><xs:complexType><xs:sequence>
     <xs:element name="item" type="xs:string"/></xs:sequence></xs:complexType></xs:element>
     </xs:schema>');

-- Element and attribute extraction with XPath
SELECT xml_extract_elements('<catalog><book id="1"><title>Book 1</title></book></catalog>', '//book');
SELECT xml_extract_attributes('<catalog><book id="1" available="true"></book></catalog>', '//book');

-- File reading with schema inference options
SELECT * FROM read_xml('books.xml');

-- Extract XML document content as-is
SELECT filename, content FROM read_xml_objects('config/*.xml', ignore_errors=true);

-- Document formatting
SELECT xml_pretty_print('<root><item>test</item></root>');
```

## Design Principles

1. **Focus on Analytics**: Optimized for data extraction and querying, not document transformation
2. **Smart Defaults**: Intelligent schema inference with extensive configuration options
3. **Standards Compliance**: Built on libxml2 for robust XML parsing and XPath support
4. **DuckDB Integration**: Follows established extension patterns from JSON/YAML/Markdown

## Architecture

- **Library**: libxml2 for robust XML parsing and XPath support
- **Type System**: Custom `XML` type with automatic VARCHAR casting
- **Memory Management**: RAII wrappers for safe C library integration
- **Schema Inference**: Configurable flattening strategies for hierarchical data
- **Dependencies**: vcpkg for cross-platform dependency management

## Excluded Features

To maintain focus and simplicity:
- XSLT transformations (use external tools)
- XQuery support (complex, limited SQL integration value)
- XML document modification (use specialized XML tools)
- DTD/Schema generation (academic feature)

## Implementation Status

### Phase 1: Foundation ✅ COMPLETE
- ✅ Basic extension structure with libxml2 integration via vcpkg
- ✅ XML type system with validation functions (`xml_valid`, `xml_well_formed`)
- ✅ XPath extraction capabilities (`xml_extract_text`, `xml_extract_all_text`)
- ✅ RAII memory management for safe libxml2 integration

### Phase 2: Schema Inference ✅ COMPLETE
- ✅ Intelligent document analysis and flattening with `XMLSchemaInference`
- ✅ File reading functions with configuration options (`read_xml`, `read_xml_objects`)
- ✅ Replacement scan support for `.xml` files (`FROM 'file.xml'` syntax)
- ✅ Smart type detection (BOOLEAN, INTEGER, DOUBLE, DATE, TIMESTAMP, TIME)

### Phase 3: XPath Integration ✅ COMPLETE
- ✅ XPath-based element extraction with libxml2 xpath context
- ✅ Attribute and content handling in schema inference
- ✅ Cross-format conversion functions (XML ↔ JSON)

### Phase 4: Utility Functions ✅ COMPLETE
- ✅ Document analysis functions (`xml_stats`, `xml_namespaces`)
- ✅ Document formatting (`xml_pretty_print`, partial `xml_minify`)
- ✅ XSD schema validation with `xml_validate_schema`
- ✅ Comprehensive utility function suite with RAII memory management

### Phase 5: Testing & Polish ✅ COMPLETE
- ✅ Comprehensive test suite covering all major functionality
- ✅ Production-ready error handling and memory management
- ✅ Documentation and examples

## Current Status

The DuckDB XML extension has reached a mature state with comprehensive XML processing capabilities:

- **Core functionality**: All major XML operations are implemented and tested
- **Schema inference**: Automatic XML-to-relational mapping with configurable options
- **Format conversion**: Seamless XML ↔ JSON conversion with proper structure preservation
- **Production ready**: Comprehensive test suite with robust error handling
- **Standards compliant**: Built on libxml2 for reliable XML parsing and XPath support

### Recent Improvements
- ✅ Implemented comprehensive XML utility function suite (10+ functions)
- ✅ Fixed XML-to-JSON conversion algorithm with proper structure handling
- ✅ Added XSD schema validation with `xml_validate_schema`
- ✅ Enhanced document analysis with `xml_stats` and `xml_namespaces`
- ✅ Implemented RAII memory management throughout with DuckDB-style smart pointers

## Known Issues

### Minor Functionality Issues
- **`xml_extract_comments()`**: Returns empty results - requires special libxml2 parsing flags to preserve comments in document tree
- **`xml_extract_cdata()`**: Returns empty results - similar issue with CDATA section preservation during parsing
- **`xml_minify()`**: Partial implementation - currently doesn't remove all insignificant whitespace

### Workarounds
- For comment extraction: Comments can be accessed via XPath expressions in some cases
- For CDATA content: Text content is accessible via `xml_extract_text()` even if CDATA structure isn't preserved
- For minification: `xml_pretty_print()` works correctly for formatting

### Status
These are low-priority issues that don't affect core XML processing functionality. The vast majority of XML analytical use cases are fully supported.

## Contributing

Contributions are welcome! The extension has solid foundations and is ready for:
- Fixing comment/CDATA extraction with proper libxml2 parsing flags
- Performance optimizations for large document processing
- Additional XPath 2.0 features and functions
- Enhanced JSON conversion with nested object handling
- Advanced schema inference features for complex document structures

## License

MIT License - following DuckDB community extension standards.
