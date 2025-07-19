-- Test original user requirements

-- Load XML extension
LOAD webbed;

-- Requirement 1: Enable SELECT '<h1>Hi</h1>'::XML
SELECT '<h1>Hi</h1>'::XML AS requirement_1_xml_cast;

-- Requirement 2: to_xml() function that converts values to XML
SELECT to_xml('Hello World') AS requirement_2_basic_to_xml;
SELECT to_xml(['ABC', 'DEF']) AS requirement_2_list_to_xml;
SELECT to_xml({'a': 1, 'b': '2'}) AS requirement_2_struct_to_xml;

-- Requirement 3: Verify xml() function works properly (should be same as to_xml now)
SELECT xml('Test') AS requirement_3_xml_function;