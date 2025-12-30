Quick Start
===========

This guide will get you started with the webbed extension in just a few minutes.

Loading the Extension
---------------------

.. code-block:: sql

   LOAD webbed;

Reading XML Files
-----------------

The simplest way to work with XML is using ``read_xml``:

.. code-block:: sql

   -- Read an XML file directly into a table
   SELECT * FROM read_xml('data.xml');

   -- Read multiple files with a glob pattern
   SELECT * FROM read_xml('config/*.xml');

   -- Read with schema inference options
   SELECT * FROM read_xml('data.xml', record_element := 'item');

Reading HTML Files
------------------

Similarly for HTML:

.. code-block:: sql

   -- Read HTML files
   SELECT * FROM read_html('page.html');

   -- Extract specific elements
   SELECT * FROM read_html('page.html', record_element := 'article');

Extracting Data with XPath
--------------------------

Use XPath expressions to extract specific content:

.. code-block:: sql

   -- Extract text from XML
   SELECT xml_extract_text('<book><title>DuckDB Guide</title></book>', '//title');
   -- Result: "DuckDB Guide"

   -- Extract from HTML
   SELECT html_extract_text('<html><body><h1>Welcome</h1></body></html>', '//h1');
   -- Result: "Welcome"

   -- Extract attributes
   SELECT xml_extract_attributes('<item id="123" type="book"/>', '/item');
   -- Result: [{id: "123", type: "book"}]

Working with Document Objects
-----------------------------

For more control, use the ``_objects`` variants:

.. code-block:: sql

   -- Get raw document objects
   SELECT xml, filename
   FROM read_xml_objects('data/*.xml', filename=true);

   -- Process each document
   SELECT
       filename,
       xml_extract_text(xml, '//title') as title,
       xml_stats(xml::VARCHAR) as stats
   FROM read_xml_objects('books/*.xml', filename=true);

Converting Between Formats
--------------------------

Convert XML to JSON and vice versa:

.. code-block:: sql

   -- XML to JSON
   SELECT xml_to_json('<person><name>John</name><age>30</age></person>');
   -- Result: {"person":{"name":{"#text":"John"},"age":{"#text":"30"}}}

   -- JSON to XML
   SELECT json_to_xml('{"name":"John","age":30}');
   -- Result: <root><name>John</name><age>30</age></root>

Extracting Links and Images from HTML
-------------------------------------

.. code-block:: sql

   -- Extract all links
   SELECT (unnest(html_extract_links(html))).href as url,
          (unnest(html_extract_links(html))).text as link_text
   FROM read_html_objects('page.html');

   -- Extract all images
   SELECT (unnest(html_extract_images(html))).src as image_url,
          (unnest(html_extract_images(html))).alt as alt_text
   FROM read_html_objects('page.html');

Extracting HTML Tables
----------------------

.. code-block:: sql

   -- Extract tables as rows
   SELECT table_index, row_index, columns
   FROM html_extract_tables('<table><tr><th>Name</th></tr><tr><td>John</td></tr></table>');

Next Steps
----------

- See :doc:`functions/index` for a complete function reference
- Learn about :doc:`parameters` for customization options
- Explore :doc:`xpath_guide` for advanced XPath queries
- Understand :doc:`schema_inference` for automatic type detection
