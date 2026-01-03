XML Extraction Functions
========================

These functions extract data from XML documents using XPath expressions.

xml_extract_text
----------------

Extract text content from all matching elements.

**Syntax:**

.. code-block:: sql

   xml_extract_text(xml, xpath)

**Parameters:**

- ``xml`` (VARCHAR or XML): The XML content to search
- ``xpath`` (VARCHAR): XPath expression to match

**Returns:** VARCHAR[] - List of text content from all matching elements, or empty list if no match.

**Examples:**

.. code-block:: sql

   -- Basic extraction (returns list)
   SELECT xml_extract_text('<book><title>DuckDB Guide</title></book>', '//title');
   -- Result: ["DuckDB Guide"]

   -- Get first match using array indexing
   SELECT xml_extract_text('<book><title>DuckDB Guide</title></book>', '//title')[1];
   -- Result: "DuckDB Guide"

   -- Multiple matches
   SELECT xml_extract_text('<catalog><book><title>Book A</title></book><book><title>Book B</title></book></catalog>', '//title');
   -- Result: ["Book A", "Book B"]

   -- Attribute extraction
   SELECT xml_extract_text('<item id="123"/>', '/item/@id');
   -- Result: ["123"]

   -- With predicates
   SELECT xml_extract_text(
       '<catalog><book category="fiction"><title>Novel</title></book></catalog>',
       '//book[@category="fiction"]/title'
   );
   -- Result: ["Novel"]

.. note::

   For namespaced documents, use ``local-name()`` to match elements regardless of namespace:

   .. code-block:: sql

      SELECT xml_extract_text(xml, '//*[local-name()="title"]')

**With Namespace Support:**

Use the ``namespaces`` named parameter to handle namespace prefixes in XPath:

.. code-block:: sql

   xml_extract_text(xml, xpath, namespaces:=<mode_or_map>)

**Namespace Modes:**

.. list-table::
   :header-rows: 1
   :widths: 15 85

   * - Mode
     - Description
   * - ``'auto'``
     - Auto-register declared namespaces, common namespaces, and mock undefined prefixes. **Recommended for most use cases.**
   * - ``'strict'``
     - Auto-register declared namespaces, raise error on undefined prefix.
   * - ``'ignore'``
     - Auto-register declared namespaces, silently return empty on undefined prefix.

**Examples:**

.. code-block:: sql

   -- Use 'auto' mode for convenience (handles undeclared prefixes)
   SELECT xml_extract_text(
       '<root><gml:pos>1 2 3</gml:pos></root>',
       '//gml:pos',
       namespaces:='auto'
   );
   -- Result: ["1 2 3"]

   -- Use explicit MAP for precise control
   SELECT xml_extract_text(
       '<root xmlns:ns="http://example.com"><ns:item>Value</ns:item></root>',
       '//ns:item',
       namespaces:=MAP {'ns': 'http://example.com'}
   );
   -- Result: ["Value"]

   -- Use helper functions
   SELECT xml_extract_text(
       '<root><gml:pos>1 2 3</gml:pos></root>',
       '//gml:pos',
       namespaces:=xml_mock_namespaces(['gml'])
   );
   -- Result: ["1 2 3"]

   -- Discover namespaces with xml_namespaces()
   SELECT xml_namespaces('<root xmlns:gml="http://www.opengis.net/gml"/>');
   -- Result: {gml: "http://www.opengis.net/gml"}


xml_extract_all_text
--------------------

Extract all text content from an XML document.

**Syntax:**

.. code-block:: sql

   xml_extract_all_text(xml)

**Parameters:**

- ``xml`` (VARCHAR or XML): The XML content

**Returns:** VARCHAR - All text content concatenated.

**Example:**

.. code-block:: sql

   SELECT xml_extract_all_text('<p>Hello <b>world</b>!</p>');
   -- Result: "Hello world!"


xml_extract_elements
--------------------

Extract all matching elements as XML fragments.

**Syntax:**

.. code-block:: sql

   xml_extract_elements(xml, xpath)

**Returns:** xmlfragment[] - List of matching elements as XML fragments.

**Example:**

.. code-block:: sql

   SELECT xml_extract_elements('<items><item id="1">First</item><item id="2">Second</item></items>', '//item');
   -- Result: [<item id="1">First</item>, <item id="2">Second</item>]

   -- Get first match
   SELECT xml_extract_elements('<items><item id="1">First</item></items>', '//item')[1];


xml_extract_elements_string
---------------------------

Extract all matching elements as newline-separated text.

**Syntax:**

.. code-block:: sql

   xml_extract_elements_string(xml, xpath)

**Returns:** VARCHAR - All matching elements serialized as XML, separated by newlines.

**Example:**

.. code-block:: sql

   SELECT xml_extract_elements_string(
       '<items><item>A</item><item>B</item></items>',
       '//item'
   );
   -- Result: "<item>A</item>\n<item>B</item>"


xml_extract_attributes
----------------------

Extract attributes from matching elements as a list of structs.

**Syntax:**

.. code-block:: sql

   xml_extract_attributes(xml, xpath)

**Returns:** LIST<STRUCT> - List of attribute key-value pairs for each matching element.

**Example:**

.. code-block:: sql

   SELECT xml_extract_attributes(
       '<items><item id="1" type="A"/><item id="2" type="B"/></items>',
       '//item'
   );
   -- Result: [{id: "1", type: "A"}, {id: "2", type: "B"}]


xml_extract_comments
--------------------

Extract all XML comments with their line numbers.

**Syntax:**

.. code-block:: sql

   xml_extract_comments(xml)

**Returns:** LIST<STRUCT(content VARCHAR, line_number INTEGER)>

**Example:**

.. code-block:: sql

   SELECT xml_extract_comments('<root><!-- This is a comment --></root>');
   -- Result: [{content: " This is a comment ", line_number: 1}]


xml_extract_cdata
-----------------

Extract all CDATA sections with their line numbers.

**Syntax:**

.. code-block:: sql

   xml_extract_cdata(xml)

**Returns:** LIST<STRUCT(content VARCHAR, line_number INTEGER)>

**Example:**

.. code-block:: sql

   SELECT xml_extract_cdata('<root><![CDATA[Some raw content]]></root>');
   -- Result: [{content: "Some raw content", line_number: 1}]
