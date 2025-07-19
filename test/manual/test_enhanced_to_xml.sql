-- Test enhanced to_xml() function

-- Load XML extension
LOAD webbed;

-- Test 1: Basic XML type casting
SELECT '<h1>Hi</h1>'::XML AS xml_cast_test;

-- Test 2: Basic to_xml() with scalar values
SELECT to_xml('Hello World') AS scalar_xml;

-- Test 3: to_xml() with custom node name
SELECT to_xml('Hello World', 'greeting') AS custom_node_xml;

-- Test 4: to_xml() with LIST
SELECT to_xml(['ABC', 'DEF']) AS list_xml;

-- Test 5: to_xml() with STRUCT
SELECT to_xml({'a': 1, 'b': '2'}) AS struct_xml;

-- Test 6: to_xml() with nested structure
SELECT to_xml({'name': 'John', 'age': 30, 'items': ['apple', 'banana']}) AS nested_xml;