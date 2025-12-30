Conversion Functions
====================

These functions convert between XML, JSON, and other formats.

xml_to_json
-----------

Convert XML to JSON with configurable options.

**Syntax:**

.. code-block:: sql

   xml_to_json(xml [, options...])

**Parameters:**

.. list-table::
   :header-rows: 1
   :widths: 20 15 65

   * - Parameter
     - Type
     - Description
   * - ``xml``
     - VARCHAR/XML
     - The XML content to convert
   * - ``force_list``
     - VARCHAR[]
     - Element names to always convert to JSON arrays
   * - ``attr_prefix``
     - VARCHAR
     - Prefix for attributes (default: ``'@'``)
   * - ``text_key``
     - VARCHAR
     - Key for text content (default: ``'#text'``)
   * - ``namespaces``
     - VARCHAR
     - Namespace handling: ``'strip'``, ``'expand'``, ``'keep'``
   * - ``xmlns_key``
     - VARCHAR
     - Key for namespace declarations (default: empty/disabled)
   * - ``empty_elements``
     - VARCHAR
     - Empty element handling: ``'object'``, ``'null'``, ``'string'``

**Returns:** VARCHAR (JSON string)

**Examples:**

.. code-block:: sql

   -- Basic conversion
   SELECT xml_to_json('<person><name>John</name><age>30</age></person>');
   -- Result: {"person":{"name":{"#text":"John"},"age":{"#text":"30"}}}

   -- Force specific elements to be arrays
   SELECT xml_to_json(
       '<catalog><book><title>Book 1</title></book></catalog>',
       force_list := ['book']
   );

   -- Custom attribute prefix and text key
   SELECT xml_to_json(
       '<item id="123">Product Name</item>',
       attr_prefix := '_',
       text_key := 'value'
   );
   -- Result: {"item":{"_id":"123","value":"Product Name"}}

   -- Handle namespaces
   SELECT xml_to_json(
       '<root xmlns:ns="http://example.com"><ns:item>Test</ns:item></root>',
       namespaces := 'keep'
   );

   -- Handle empty elements as null
   SELECT xml_to_json('<root><item/></root>', empty_elements := 'null');
   -- Result: {"root":{"item":null}}


json_to_xml
-----------

Convert JSON to XML.

**Syntax:**

.. code-block:: sql

   json_to_xml(json)

**Parameters:**

- ``json`` (VARCHAR): JSON string to convert

**Returns:** VARCHAR (XML string)

**Example:**

.. code-block:: sql

   SELECT json_to_xml('{"name":"John","age":30}');
   -- Result: <root><name>John</name><age>30</age></root>


to_xml
------

Convert any value to XML.

**Syntax:**

.. code-block:: sql

   to_xml(value)
   to_xml(value, node_name)

**Parameters:**

- ``value``: Any value to convert
- ``node_name`` (VARCHAR, optional): Custom node name for the root element

**Returns:** VARCHAR (XML string)

**Examples:**

.. code-block:: sql

   -- Convert string
   SELECT to_xml('Hello World');
   -- Result: <value>Hello World</value>

   -- With custom node name
   SELECT to_xml('John Doe', 'author');
   -- Result: <author>John Doe</author>

   -- Convert number
   SELECT to_xml(42, 'count');
   -- Result: <count>42</count>


xml (alias)
-----------

Alias for ``to_xml``.

**Syntax:**

.. code-block:: sql

   xml(value)

**Example:**

.. code-block:: sql

   SELECT xml('Hello');
   -- Result: <value>Hello</value>


Python xmltodict Compatibility
------------------------------

For Python-style xmltodict behavior, create a macro:

.. code-block:: sql

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
