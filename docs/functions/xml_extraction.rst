XML Extraction Functions
========================

These functions extract data from XML documents using XPath expressions.

xml_extract_text
----------------

Extract text content from the first matching element.

**Syntax:**

.. code-block:: sql

   xml_extract_text(xml, xpath)

**Parameters:**

- ``xml`` (VARCHAR or XML): The XML content to search
- ``xpath`` (VARCHAR): XPath expression to match

**Returns:** VARCHAR - The text content of the first matching element, or empty string if no match.

**Examples:**

.. code-block:: sql

   -- Basic extraction
   SELECT xml_extract_text('<book><title>DuckDB Guide</title></book>', '//title');
   -- Result: "DuckDB Guide"

   -- Attribute extraction
   SELECT xml_extract_text('<item id="123"/>', '/item/@id');
   -- Result: "123"

   -- With predicates
   SELECT xml_extract_text(
       '<catalog><book category="fiction"><title>Novel</title></book></catalog>',
       '//book[@category="fiction"]/title'
   );
   -- Result: "Novel"

.. note::

   For namespaced documents, use ``local-name()`` to match elements regardless of namespace:

   .. code-block:: sql

      SELECT xml_extract_text(xml, '//*[local-name()="title"]')


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

Extract the first matching element as a structured value.

**Syntax:**

.. code-block:: sql

   xml_extract_elements(xml, xpath)

**Returns:** STRUCT - The element with its attributes and child elements.

**Example:**

.. code-block:: sql

   SELECT xml_extract_elements('<items><item id="1">First</item></items>', '//item');


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
