HTML Extraction Functions
=========================

These functions extract data from HTML documents.

html_extract_text
-----------------

Extract text content from HTML.

**Syntax:**

.. code-block:: sql

   html_extract_text(html)
   html_extract_text(html, xpath)

**Parameters:**

- ``html`` (VARCHAR or HTML): The HTML content
- ``xpath`` (VARCHAR, optional): XPath expression to match specific elements

**Returns:** VARCHAR - The extracted text content.

**Examples:**

.. code-block:: sql

   -- Extract all text
   SELECT html_extract_text('<html><body><p>Hello World</p></body></html>');
   -- Result: "Hello World"

   -- Extract specific element
   SELECT html_extract_text('<html><body><h1>Title</h1><p>Body</p></body></html>', '//h1');
   -- Result: "Title"

.. note::

   When using XPath, only the first matching element's text is returned.


html_extract_links
------------------

Extract all hyperlinks from HTML with metadata.

**Syntax:**

.. code-block:: sql

   html_extract_links(html)

**Returns:** LIST<STRUCT(text VARCHAR, href VARCHAR, title VARCHAR, line_number INTEGER)>

**Example:**

.. code-block:: sql

   SELECT html_extract_links(
       '<a href="/home" title="Home Page">Home</a><a href="/about">About</a>'
   );
   -- Result: [
   --   {text: "Home", href: "/home", title: "Home Page", line_number: 1},
   --   {text: "About", href: "/about", title: NULL, line_number: 1}
   -- ]

   -- Unnest to get individual links
   SELECT (unnest(html_extract_links(html))).href as url
   FROM read_html_objects('page.html');


html_extract_images
-------------------

Extract all images from HTML with metadata.

**Syntax:**

.. code-block:: sql

   html_extract_images(html)

**Returns:** LIST<STRUCT(alt VARCHAR, src VARCHAR, title VARCHAR, width INTEGER, height INTEGER, line_number INTEGER)>

**Example:**

.. code-block:: sql

   SELECT html_extract_images(
       '<img src="photo.jpg" alt="A photo" width="800" height="600">'
   );
   -- Result: [{alt: "A photo", src: "photo.jpg", title: NULL, width: 800, height: 600, line_number: 1}]


html_extract_tables
-------------------

Extract HTML tables as rows (table function).

**Syntax:**

.. code-block:: sql

   SELECT * FROM html_extract_tables(html)

**Returns:** TABLE(table_index INTEGER, row_index INTEGER, columns VARCHAR[])

**Example:**

.. code-block:: sql

   SELECT * FROM html_extract_tables(
       '<table><tr><th>Name</th><th>Age</th></tr><tr><td>John</td><td>30</td></tr></table>'
   );
   -- Result:
   -- table_index | row_index | columns
   -- 0           | 0         | ["Name", "Age"]
   -- 0           | 1         | ["John", "30"]


html_extract_table_rows
-----------------------

Extract table data as structured rows.

**Syntax:**

.. code-block:: sql

   html_extract_table_rows(html)

**Returns:** LIST<STRUCT> - Structured table data.


html_extract_tables_json
------------------------

Extract tables with rich JSON structure including headers.

**Syntax:**

.. code-block:: sql

   html_extract_tables_json(html)

**Returns:** LIST<STRUCT(headers VARCHAR[], rows VARCHAR[][], row_count INTEGER)>


html_escape
-----------

Escape HTML special characters.

**Syntax:**

.. code-block:: sql

   html_escape(text)

**Parameters:**

- ``text`` (VARCHAR): Text to escape

**Returns:** VARCHAR - Text with HTML entities escaped.

**Example:**

.. code-block:: sql

   SELECT html_escape('<p>Hello & World</p>');
   -- Result: "&lt;p&gt;Hello &amp; World&lt;/p&gt;"


html_unescape
-------------

Decode HTML entities to text.

**Syntax:**

.. code-block:: sql

   html_unescape(text)

**Parameters:**

- ``text`` (VARCHAR): Text with HTML entities

**Returns:** VARCHAR - Decoded text.

**Example:**

.. code-block:: sql

   SELECT html_unescape('&lt;p&gt;Hello &amp; World&lt;/p&gt;');
   -- Result: "<p>Hello & World</p>"


parse_html
----------

Parse an HTML string into the HTML type.

**Syntax:**

.. code-block:: sql

   parse_html(content)

**Parameters:**

- ``content`` (VARCHAR): HTML string to parse

**Returns:** HTML - Parsed HTML document.

**Example:**

.. code-block:: sql

   SELECT parse_html('<html><body><p>Hello</p></body></html>');
