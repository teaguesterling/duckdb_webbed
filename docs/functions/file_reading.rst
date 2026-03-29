File Reading & String Parsing Functions
=======================================

These functions read XML and HTML from files or parse them from strings directly into DuckDB tables.

read_xml
--------

Read XML files with automatic schema inference.

**Syntax:**

.. code-block:: sql

   read_xml(pattern [, options...])

**Parameters:**

.. list-table::
   :header-rows: 1
   :widths: 20 15 65

   * - Parameter
     - Type
     - Description
   * - ``pattern``
     - VARCHAR
     - File path or glob pattern (e.g., ``'data.xml'``, ``'*.xml'``)
   * - ``filename``
     - BOOLEAN
     - Include filename column in output (default: false)
   * - ``ignore_errors``
     - BOOLEAN
     - Skip files that fail to parse (default: false)
   * - ``maximum_file_size``
     - BIGINT
     - Maximum file size in bytes (default: 128MB)
   * - ``record_element``
     - VARCHAR
     - XPath or tag name for elements that become rows
   * - ``root_element``
     - VARCHAR
     - Specify root element for schema inference
   * - ``force_list``
     - VARCHAR[]
     - Column names that should always be LIST type
   * - ``auto_detect``
     - BOOLEAN
     - Enable automatic schema detection (default: true)
   * - ``max_depth``
     - INTEGER
     - Maximum nesting depth to parse (default: 10)
   * - ``all_varchar``
     - BOOLEAN
     - Force all scalar types to VARCHAR (default: false)
   * - ``union_by_name``
     - BOOLEAN
     - Combine columns by name for multiple files (default: false)
   * - ``attr_mode``
     - VARCHAR
     - Attribute handling: 'prefix', 'merge', 'ignore' (default: 'prefix')
   * - ``attr_prefix``
     - VARCHAR
     - Prefix for attribute columns (default: '@')
   * - ``text_key``
     - VARCHAR
     - Key for text content in mixed elements (default: '#text')
   * - ``empty_elements``
     - VARCHAR
     - Empty element handling: 'object', 'null', 'string' (default: 'object')
   * - ``namespaces``
     - VARCHAR
     - Namespace handling: 'strip', 'expand', 'keep' (default: 'strip')
   * - ``columns``
     - STRUCT
     - Explicit column schema (e.g., ``{name: 'VARCHAR', price: 'DOUBLE'}``)
   * - ``datetime_format``
     - VARCHAR or VARCHAR[]
     - Controls date/time detection. Accepts ``'auto'`` (default), ``'none'``, preset names (``'us'``, ``'eu'``, ``'iso'``), custom strftime strings, or a list of formats.
   * - ``nullstr``
     - VARCHAR or VARCHAR[]
     - String value(s) to interpret as NULL (e.g., ``'N/A'`` or ``['N/A', '-']``)
   * - ``streaming``
     - BOOLEAN
     - Enable SAX streaming for files exceeding ``maximum_file_size`` (default: true). SAX mode only supports simple tag names for ``record_element``. Not available for HTML.

**Examples:**

.. code-block:: sql

   -- Basic usage
   SELECT * FROM read_xml('catalog.xml');

   -- With glob pattern
   SELECT * FROM read_xml('data/*.xml', filename=true);

   -- Extract specific records
   SELECT * FROM read_xml('feed.xml', record_element := 'item');

   -- Force array type for optional elements
   SELECT * FROM read_xml('products.xml',
       record_element := 'product',
       force_list := ['tag', 'category']
   );

   -- Combine files with different schemas
   SELECT * FROM read_xml('configs/*.xml', union_by_name := true);


read_xml_objects
----------------

Read XML files as raw document objects for custom processing.

**Syntax:**

.. code-block:: sql

   read_xml_objects(pattern [, filename=false, ignore_errors=false])

**Returns:**

A table with column ``xml`` of type ``XML``. If ``filename=true``, also includes ``filename`` column.

**Examples:**

.. code-block:: sql

   -- Get raw XML documents
   SELECT xml FROM read_xml_objects('data.xml');

   -- Process with XPath
   SELECT
       filename,
       xml_extract_text(xml, '//title') as title
   FROM read_xml_objects('books/*.xml', filename=true);


read_html
---------

Read HTML files with automatic schema inference.

**Syntax:**

.. code-block:: sql

   read_html(pattern [, options...])

**Parameters:**

Same parameters as ``read_xml``, plus HTML-specific handling.

**Examples:**

.. code-block:: sql

   -- Extract articles from HTML pages
   SELECT * FROM read_html('pages/*.html', record_element := 'article');


read_html_objects
-----------------

Read HTML files as raw document objects.

**Syntax:**

.. code-block:: sql

   read_html_objects(pattern [, filename=false, ignore_errors=false])

**Returns:**

A table with column ``html`` of type ``HTML``. If ``filename=true``, also includes ``filename`` column.

**Examples:**

.. code-block:: sql

   -- Get raw HTML documents
   SELECT html FROM read_html_objects('page.html');

   -- Extract links from multiple pages
   SELECT
       filename,
       html_extract_links(html) as links
   FROM read_html_objects('pages/*.html', filename=true);


String Parsing Functions
------------------------

These functions parse XML and HTML content directly from strings, complementing the file-based ``read_*`` functions.

parse_xml
~~~~~~~~~

Parse XML string with automatic schema inference.

**Syntax:**

.. code-block:: sql

   parse_xml(content [, options...])

**Parameters:**

.. list-table::
   :header-rows: 1
   :widths: 20 15 65

   * - Parameter
     - Type
     - Description
   * - ``content``
     - VARCHAR
     - XML content to parse
   * - ``ignore_errors``
     - BOOLEAN
     - Return empty result instead of error on invalid input (default: false)
   * - ``record_element``
     - VARCHAR
     - XPath or tag name for elements that become rows
   * - ``root_element``
     - VARCHAR
     - Specify root element for schema inference
   * - ``force_list``
     - VARCHAR[]
     - Column names that should always be LIST type
   * - ``auto_detect``
     - BOOLEAN
     - Enable automatic schema detection (default: true)
   * - ``max_depth``
     - INTEGER
     - Maximum nesting depth to parse (default: 10)
   * - ``all_varchar``
     - BOOLEAN
     - Force all scalar types to VARCHAR (default: false)
   * - ``columns``
     - STRUCT
     - Explicit column schema (e.g., ``{item: 'INTEGER'}``)

**Examples:**

.. code-block:: sql

   -- Parse XML string with schema inference
   SELECT * FROM parse_xml('<catalog><book><title>DuckDB</title><price>29.99</price></book></catalog>');

   -- Parse multiple records
   SELECT name, value FROM parse_xml(
       '<items><item><name>A</name><value>1</value></item><item><name>B</name><value>2</value></item></items>'
   );

   -- With explicit schema
   SELECT * FROM parse_xml('<root><item>42</item></root>', columns := {item: 'INTEGER'});

   -- Ignore parse errors
   SELECT * FROM parse_xml(xml_column, ignore_errors := true) FROM raw_data;


parse_xml_objects
~~~~~~~~~~~~~~~~~

Parse XML string and return as raw XML type.

**Syntax:**

.. code-block:: sql

   parse_xml_objects(content [, ignore_errors=false])

**Returns:**

A table with column ``xml`` of type ``XML``.

**Examples:**

.. code-block:: sql

   -- Parse and validate XML string
   SELECT * FROM parse_xml_objects('<root><item>test</item></root>');

   -- Process with XPath
   SELECT xml_extract_text(xml, '//item') as items
   FROM parse_xml_objects('<root><item>A</item><item>B</item></root>');


parse_html
~~~~~~~~~~

Parse HTML string with automatic schema inference.

**Syntax:**

.. code-block:: sql

   parse_html(content [, options...])

**Parameters:**

Same parameters as ``parse_xml``. HTML parsing is more lenient than XML parsing.

**Examples:**

.. code-block:: sql

   -- Parse HTML content
   SELECT * FROM parse_html('<html><body><div>Content</div></body></html>');

   -- Extract from HTML with record element
   SELECT * FROM parse_html('<ul><li>Item 1</li><li>Item 2</li></ul>', record_element := 'li');


parse_html_objects
~~~~~~~~~~~~~~~~~~

Parse HTML string and return as raw HTML type.

**Syntax:**

.. code-block:: sql

   parse_html_objects(content [, ignore_errors=false])

**Returns:**

A table with column ``html`` of type ``HTML``.

**Examples:**

.. code-block:: sql

   -- Parse HTML string
   SELECT * FROM parse_html_objects('<div><p>Hello World</p></div>');

   -- Process with extraction functions
   SELECT html_extract_text(html) as text
   FROM parse_html_objects('<p>Paragraph <strong>with bold</strong> text.</p>');
