Utility Functions
=================

These functions provide validation, analysis, and formatting capabilities for XML documents.

Validation
----------

xml_valid
~~~~~~~~~

Check if XML is well-formed.

**Syntax:**

.. code-block:: sql

   xml_valid(xml)

**Parameters:**

- ``xml`` (VARCHAR or XML): The XML content to validate

**Returns:** BOOLEAN - ``true`` if the XML is well-formed, ``false`` otherwise.

**Example:**

.. code-block:: sql

   SELECT xml_valid('<root><item>Valid</item></root>');  -- true
   SELECT xml_valid('<root><item>Invalid</root>');       -- false


xml_well_formed
~~~~~~~~~~~~~~~

Alias for ``xml_valid``.

**Syntax:**

.. code-block:: sql

   xml_well_formed(xml)


xml_validate_schema
~~~~~~~~~~~~~~~~~~~

Validate XML against an XSD schema.

**Syntax:**

.. code-block:: sql

   xml_validate_schema(xml, xsd)

**Parameters:**

- ``xml`` (VARCHAR): The XML content to validate
- ``xsd`` (VARCHAR): The XSD schema to validate against

**Returns:** BOOLEAN - ``true`` if the XML is valid according to the schema.

**Example:**

.. code-block:: sql

   SELECT xml_validate_schema(
       '<person><name>John</name></person>',
       '<xs:schema xmlns:xs="http://www.w3.org/2001/XMLSchema">
         <xs:element name="person">
           <xs:complexType>
             <xs:sequence>
               <xs:element name="name" type="xs:string"/>
             </xs:sequence>
           </xs:complexType>
         </xs:element>
       </xs:schema>'
   );


Analysis
--------

xml_stats
~~~~~~~~~

Get document statistics.

**Syntax:**

.. code-block:: sql

   xml_stats(xml)

**Returns:** STRUCT with fields:

- ``element_count`` (INTEGER): Number of elements
- ``attribute_count`` (INTEGER): Number of attributes
- ``text_node_count`` (INTEGER): Number of text nodes
- ``comment_count`` (INTEGER): Number of comments
- ``max_depth`` (INTEGER): Maximum nesting depth
- ``namespace_count`` (INTEGER): Number of namespaces

**Example:**

.. code-block:: sql

   SELECT xml_stats('<root><item id="1">Text</item><item id="2"/></root>');
   -- Result: {element_count: 3, attribute_count: 2, text_node_count: 1, ...}

   -- Access individual stats
   SELECT (xml_stats(xml)).element_count FROM read_xml_objects('doc.xml');


xml_namespaces
~~~~~~~~~~~~~~

List all namespaces declared in an XML document.

**Syntax:**

.. code-block:: sql

   xml_namespaces(xml)

**Returns:** LIST<STRUCT(prefix VARCHAR, uri VARCHAR)>

**Example:**

.. code-block:: sql

   SELECT xml_namespaces(
       '<root xmlns="http://default.com" xmlns:ns="http://example.com"/>'
   );
   -- Result: [{prefix: "", uri: "http://default.com"}, {prefix: "ns", uri: "http://example.com"}]


xml_common_namespaces
~~~~~~~~~~~~~~~~~~~~~

Returns a MAP of well-known namespace prefixes to their URIs (XML, XHTML, SVG, RSS, Atom, SOAP, etc.).

**Syntax:**

.. code-block:: sql

   xml_common_namespaces()

**Returns:** MAP(VARCHAR, VARCHAR) - Mapping of common prefixes to URIs.

**Example:**

.. code-block:: sql

   SELECT xml_common_namespaces();
   -- Result: {xml: "http://www.w3.org/XML/1998/namespace", xhtml: "http://www.w3.org/1999/xhtml", ...}

   -- Use with XPath extraction
   SELECT xml_extract_text(xml, '//atom:entry/atom:title', xml_common_namespaces());


xml_detect_prefixes
~~~~~~~~~~~~~~~~~~~

Detect namespace prefixes used in an XPath expression.

**Syntax:**

.. code-block:: sql

   xml_detect_prefixes(xpath)

**Returns:** LIST(VARCHAR) - List of unique namespace prefixes found in the XPath.

**Example:**

.. code-block:: sql

   SELECT xml_detect_prefixes('//gml:pos | //ns:item[@xlink:href]');
   -- Result: ["gml", "ns", "xlink"]


xml_mock_namespaces
~~~~~~~~~~~~~~~~~~~

Create mock namespace URIs for a list of prefixes. Useful when namespace URIs are unknown or don't matter.

**Syntax:**

.. code-block:: sql

   xml_mock_namespaces(prefixes)

**Parameters:**

- ``prefixes`` (LIST(VARCHAR)): List of namespace prefixes

**Returns:** MAP(VARCHAR, VARCHAR) - Mapping of prefixes to mock URIs (``urn:mock:prefix``).

**Example:**

.. code-block:: sql

   SELECT xml_mock_namespaces(['gml', 'ns']);
   -- Result: {gml: "urn:mock:gml", ns: "urn:mock:ns"}

   -- Use with XPath when you don't know the actual namespace URI
   SELECT xml_extract_text(
       '<root><gml:pos>1 2 3</gml:pos></root>',
       '//gml:pos',
       xml_mock_namespaces(['gml'])
   );
   -- Result: ["1 2 3"]

   -- Auto-detect and mock in one step
   SELECT xml_extract_text(
       xml,
       xpath,
       xml_mock_namespaces(xml_detect_prefixes(xpath))
   );


xml_add_namespace_declarations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Add namespace declarations to an XML document's root element.

**Syntax:**

.. code-block:: sql

   xml_add_namespace_declarations(xml, namespaces)

**Parameters:**

- ``xml`` (VARCHAR): The XML content
- ``namespaces`` (MAP(VARCHAR, VARCHAR)): Map of prefix to URI pairs to add

**Returns:** VARCHAR - XML with namespace declarations added to the root element.

**Example:**

.. code-block:: sql

   -- Add a single namespace declaration
   SELECT xml_add_namespace_declarations(
       '<root><gml:pos>1 2</gml:pos></root>',
       MAP {'gml': 'http://www.opengis.net/gml'}
   );
   -- Result: <root xmlns:gml="http://www.opengis.net/gml"><gml:pos>1 2</gml:pos></root>

   -- Fix undeclared prefixes using helper functions
   SELECT xml_add_namespace_declarations(
       xml,
       xml_mock_namespaces(xml_detect_prefixes('//gml:pos'))
   );


Formatting
----------

xml_pretty_print
~~~~~~~~~~~~~~~~

Format XML with indentation for readability.

**Syntax:**

.. code-block:: sql

   xml_pretty_print(xml)

**Returns:** VARCHAR - Formatted XML string.

**Example:**

.. code-block:: sql

   SELECT xml_pretty_print('<root><item><name>Test</name></item></root>');
   -- Result:
   -- <root>
   --   <item>
   --     <name>Test</name>
   --   </item>
   -- </root>


xml_minify
~~~~~~~~~~

Remove whitespace from XML.

**Syntax:**

.. code-block:: sql

   xml_minify(xml)

**Returns:** VARCHAR - Minified XML string with no unnecessary whitespace.

**Example:**

.. code-block:: sql

   SELECT xml_minify('<root>
       <item>
           <name>Product</name>
       </item>
   </root>');
   -- Result: <root><item><name>Product</name></item></root>


xml_wrap_fragment
~~~~~~~~~~~~~~~~~

Wrap an XML fragment with a root element.

**Syntax:**

.. code-block:: sql

   xml_wrap_fragment(fragment, wrapper)

**Parameters:**

- ``fragment`` (VARCHAR): XML fragment to wrap
- ``wrapper`` (VARCHAR): Name of the wrapper element

**Returns:** VARCHAR - Wrapped XML.

**Example:**

.. code-block:: sql

   SELECT xml_wrap_fragment('<item>A</item><item>B</item>', 'items');
   -- Result: <items><item>A</item><item>B</item></items>


System Information
------------------

xml_libxml2_version
~~~~~~~~~~~~~~~~~~~

Get libxml2 version information.

**Syntax:**

.. code-block:: sql

   xml_libxml2_version(component)

**Parameters:**

- ``component`` (VARCHAR): Component name (e.g., ``'xml'``)

**Returns:** VARCHAR - Version string.

**Example:**

.. code-block:: sql

   SELECT xml_libxml2_version('xml');
   -- Result: "2.12.6" (or similar version string)
