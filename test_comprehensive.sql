-- Test comprehensive XML utility functions
LOAD 'build/release/extension/xml/xml.duckdb_extension';

-- Test 1: XML validation
SELECT 'XML Validation Tests' as test_category;
SELECT xml_valid('<root><item>test</item></root>') AS valid_xml_test;
SELECT xml_valid('<root><item>test</root>') AS invalid_xml_test;

-- Test 2: XML to JSON conversion
SELECT 'XML to JSON Conversion Tests' as test_category;
SELECT xml_to_json('<catalog><book id="1" available="true"><title>Database Systems</title><author>John Doe</author><price>49.99</price></book></catalog>') AS complex_conversion;

-- Test 3: JSON to XML conversion
SELECT 'JSON to XML Conversion Tests' as test_category;
SELECT json_to_xml('{"root":{"name":"test","value":"123"}}') AS json_to_xml_test;

-- Test 4: XML stats
SELECT 'XML Statistics Tests' as test_category;
SELECT xml_stats('<catalog><book id="1" available="true"><title>Database Systems</title><author>John Doe</author><price>49.99</price></book><book id="2"><title>Advanced SQL</title></book></catalog>') AS stats_test;

-- Test 5: XML namespaces
SELECT 'XML Namespace Tests' as test_category;
SELECT xml_namespaces('<root xmlns:book="http://example.com/book" xmlns="http://default.com"><book:item>test</book:item></root>') AS namespace_test;

-- Test 6: XML pretty printing
SELECT 'XML Pretty Printing Tests' as test_category;
SELECT xml_pretty_print('<root><item>test</item><item>test2</item></root>') AS pretty_print_test;

-- Test 7: Element extraction
SELECT 'XML Element Extraction Tests' as test_category;
SELECT xml_extract_elements('<catalog><book id="1"><title>Book 1</title></book><book id="2"><title>Book 2</title></book></catalog>', '//book') AS element_extraction_test;

-- Test 8: Text extraction
SELECT 'XML Text Extraction Tests' as test_category;
SELECT xml_extract_text('<catalog><book><title>Database Systems</title></book></catalog>', '//title') AS text_extraction_test;

-- Test 9: Schema validation
SELECT 'XML Schema Validation Tests' as test_category;
SELECT xml_validate_schema('<root><item>test</item></root>', '<?xml version="1.0"?><xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema"><xs:element name="root"><xs:complexType><xs:sequence><xs:element name="item" type="xs:string"/></xs:sequence></xs:complexType></xs:element></xs:schema>') AS schema_validation_test;