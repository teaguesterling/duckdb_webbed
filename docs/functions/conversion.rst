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


html_to_doc_blocks
------------------

Convert HTML content into a list of structured document blocks. This function parses HTML and extracts block-level elements (headings, paragraphs, code blocks, lists, tables, etc.) into a structured format suitable for document processing and analysis.

**Syntax:**

.. code-block:: sql

   html_to_doc_blocks(html)

**Parameters:**

- ``html`` (HTML/VARCHAR): The HTML content to parse

**Returns:** ``LIST(doc_block)`` - A list of document blocks

**The doc_block Type:**

Each block is a struct with the following fields:

.. list-table::
   :header-rows: 1
   :widths: 20 15 65

   * - Field
     - Type
     - Description
   * - ``block_type``
     - VARCHAR
     - Type of block: ``'heading'``, ``'paragraph'``, ``'code'``, ``'list'``, ``'blockquote'``, ``'table'``, ``'hr'``, ``'image'``, ``'figure'``
   * - ``content``
     - VARCHAR
     - Text content of the block (or JSON for complex blocks)
   * - ``level``
     - INTEGER
     - Heading level (1-6) or blockquote nesting depth
   * - ``encoding``
     - VARCHAR
     - Content encoding: ``'text'`` for plain text, ``'json'`` for structured content
   * - ``attributes``
     - MAP(VARCHAR, VARCHAR)
     - Additional attributes (id, class, language, src, alt, etc.)
   * - ``block_order``
     - INTEGER
     - Zero-based position of the block in the document

**Block Type Details:**

.. list-table::
   :header-rows: 1
   :widths: 15 85

   * - Block Type
     - Description
   * - ``heading``
     - H1-H6 elements. ``level`` indicates heading level (1-6). ``attributes`` may contain ``id``.
   * - ``paragraph``
     - P elements. Content is plain text.
   * - ``code``
     - PRE/CODE elements. ``attributes['language']`` contains the programming language if specified.
   * - ``list``
     - UL/OL elements. ``encoding`` is ``'json'``. ``attributes['ordered']`` is ``'true'`` or ``'false'``.
   * - ``blockquote``
     - BLOCKQUOTE elements. ``level`` indicates nesting depth.
   * - ``table``
     - TABLE elements. ``encoding`` is ``'json'`` with rows/cells structure.
   * - ``hr``
     - HR elements. Content is empty.
   * - ``image``
     - IMG elements. ``attributes`` contains ``src`` and ``alt``.
   * - ``figure``
     - FIGURE elements with optional caption.

**Examples:**

.. code-block:: sql

   -- Extract blocks from HTML
   SELECT html_to_doc_blocks('<h1>Title</h1><p>Some text</p>');
   -- Returns list with 2 blocks: heading and paragraph

   -- Get all headings from a document
   SELECT block.content, block.level
   FROM (SELECT unnest(html_to_doc_blocks(html)) as block FROM documents)
   WHERE block.block_type = 'heading';

   -- Count blocks by type
   SELECT block.block_type, COUNT(*)
   FROM (SELECT unnest(html_to_doc_blocks(html)) as block FROM documents)
   GROUP BY block.block_type;

   -- Extract code blocks with their language
   SELECT block.content, block.attributes['language'] as language
   FROM (SELECT unnest(html_to_doc_blocks(
       '<pre><code class="language-python">print("hello")</code></pre>'
   )) as block)
   WHERE block.block_type = 'code';


doc_blocks_to_html
------------------

Convert a list of document blocks back to HTML. This is the inverse of ``html_to_doc_blocks``.

**Syntax:**

.. code-block:: sql

   doc_blocks_to_html(blocks)

**Parameters:**

- ``blocks`` (LIST(doc_block)): A list of document blocks

**Returns:** HTML - The reconstructed HTML content

**Examples:**

.. code-block:: sql

   -- Round-trip conversion
   SELECT doc_blocks_to_html(html_to_doc_blocks('<h1>Title</h1><p>Text</p>'));
   -- Result: <h1>Title</h1><p>Text</p>

   -- Filter and reconstruct (keep only headings and paragraphs)
   SELECT doc_blocks_to_html(
       list_filter(
           html_to_doc_blocks(html),
           block -> block.block_type IN ('heading', 'paragraph')
       )
   ) FROM documents;

   -- Reorder blocks
   SELECT doc_blocks_to_html(
       list_sort(
           html_to_doc_blocks(html),
           block -> block.block_order DESC
       )
   ) FROM documents;


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
