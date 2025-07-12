# DuckDB XML Extension

A thoughtful XML processing extension for DuckDB that enables SQL-native analysis of XML documents with intelligent schema inference and powerful XPath-based data extraction.

## Status: Design Phase

This extension is currently in the design phase. See [XML_EXTENSION_DESIGN.md](XML_EXTENSION_DESIGN.md) for the comprehensive technical design.

## Vision

Enable seamless XML data processing in DuckDB with:

- **Intelligent Schema Inference**: Automatically flatten XML documents into relational tables
- **XPath Integration**: Powerful element selection and data extraction
- **Cross-format Compatibility**: Seamless XML ↔ JSON conversion
- **SQL-native Processing**: Query XML files directly using `FROM 'file.xml'` syntax

## Planned Features

### Core Functions
```sql
-- File reading with automatic schema inference
read_xml(files, [options...]) → table
read_xml_objects(files, [options...]) → table  -- preserve document structure

-- XPath-based extraction
xml_extract_elements(xml_content, xpath) → LIST<STRUCT>
xml_extract_text(xml_content, xpath) → VARCHAR

-- Type conversion and validation
xml_to_json(xml_content) → JSON
xml_valid(content) → BOOLEAN
```

### Example Usage
```sql
-- Load extension
LOAD xml;

-- Query XML files directly  
SELECT * FROM 'catalog.xml';
SELECT * FROM 'data/*.xml' WHERE available = true;

-- XPath-based extraction
SELECT title, price 
FROM read_xml('books.xml')
WHERE xml_extract_text(content, '//genre') = 'fiction';

-- Convert to JSON for complex analysis
SELECT xml_to_json(content) AS json_data
FROM read_xml_objects('config.xml');
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

## Implementation Roadmap

### Phase 1: Foundation
- Basic extension structure with libxml2 integration
- XML type system with validation functions
- Simple extraction capabilities

### Phase 2: Schema Inference  
- Intelligent document analysis and flattening
- File reading functions with configuration options
- Replacement scan support for `.xml` files

### Phase 3: XPath Integration
- Full XPath-based element extraction
- Advanced attribute and content handling
- Cross-format conversion functions

### Phase 4: Optimization & Polish
- Performance optimization and streaming support
- Comprehensive documentation and examples
- Advanced configuration options

## Contributing

This extension is in early design phase. Contributions to the design document and architecture planning are welcome.

## License

MIT License - following DuckDB community extension standards.