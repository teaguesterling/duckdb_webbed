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
xml_extract_all_text(xml_content, xpath) → VARCHAR[]     -- ✅ IMPLEMENTED

-- Validation and utility functions
xml_valid(content) → BOOLEAN                             -- ✅ IMPLEMENTED
xml_well_formed(content) → BOOLEAN                       -- ✅ IMPLEMENTED
xml_libxml2_version() → VARCHAR                          -- ✅ IMPLEMENTED

-- Schema inference with configurable options
-- Supports: root_element, include_attributes, auto_detect, schema_depth
```

### Example Usage
```sql
-- Load extension
LOAD xml;

-- Query XML files directly with replacement scan
SELECT * FROM 'catalog.xml';
SELECT * FROM 'data/*.xml' WHERE available = true;

-- Basic XML validation and XPath extraction
SELECT xml_valid('<root><item>test</item></root>');
SELECT xml_extract_text('<books><book>Title</book></books>', '//book');

-- File reading with schema inference options
SELECT * FROM read_xml('books.xml', 
    root_element => 'book',
    include_attributes => true,
    auto_detect => true,
    schema_depth => 2
);

-- Extract XML document content as-is
SELECT filename, content FROM read_xml_objects('config/*.xml', ignore_errors => true);

-- XPath-based data extraction
SELECT xml_extract_all_text(content, '//price') AS prices
FROM read_xml_objects('catalog.xml');
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

### Phase 3: XPath Integration ✅ MOSTLY COMPLETE
- ✅ XPath-based element extraction with libxml2 xpath context
- ✅ Attribute and content handling in schema inference
- 🔄 Cross-format conversion functions (JSON conversion planned)

### Phase 4: Optimization & Polish 🔄 IN PROGRESS
- 🔄 Performance optimization for large documents
- ✅ Comprehensive test suite with 31 passing assertions
- 🔄 Advanced configuration options and edge case handling

## Current Status

The DuckDB XML extension has reached a mature state with comprehensive XML processing capabilities:

- **Core functionality**: All basic XML operations are implemented and tested
- **Schema inference**: Automatic XML-to-relational mapping with configurable options
- **Production ready**: 31 passing test assertions with robust error handling
- **Standards compliant**: Built on libxml2 for reliable XML parsing and XPath support

### Recent Improvements
- Fixed frequency calculation in schema inference engine
- Enhanced XPath text extraction with support for multiple results
- Improved error handling with `ignore_errors` parameter
- Added comprehensive configuration options for schema detection

## Contributing

Contributions are welcome! The extension has solid foundations and is ready for:
- Performance optimizations
- Additional XPath functions
- JSON conversion utilities
- Advanced schema inference features

## License

MIT License - following DuckDB community extension standards.