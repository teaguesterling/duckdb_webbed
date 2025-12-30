File Reading Functions
======================

These functions read XML and HTML files directly into DuckDB tables.

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
     - Maximum file size in bytes (default: 16MB)
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
