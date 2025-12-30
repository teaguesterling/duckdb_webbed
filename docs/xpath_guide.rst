XPath Guide
===========

The webbed extension supports XPath 1.0 expressions for extracting data from XML and HTML documents.

Basic Syntax
------------

XPath uses path expressions to navigate XML documents:

.. code-block:: sql

   -- Select all <title> elements anywhere in document
   SELECT xml_extract_text(xml, '//title');

   -- Select <title> elements that are children of <book>
   SELECT xml_extract_text(xml, '/catalog/book/title');

   -- Select the first <item> element
   SELECT xml_extract_text(xml, '//item[1]');

Path Expressions
----------------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Expression
     - Description
   * - ``/``
     - Root element
   * - ``//``
     - Descendants at any depth
   * - ``.``
     - Current node
   * - ``..``
     - Parent node
   * - ``@``
     - Attribute selector
   * - ``*``
     - Any element

Examples
~~~~~~~~

.. code-block:: sql

   -- Absolute path from root
   SELECT xml_extract_text(xml, '/catalog/book/title');

   -- Any <title> anywhere
   SELECT xml_extract_text(xml, '//title');

   -- All child elements of current node
   SELECT xml_extract_text(xml, '//*');

Attribute Selection
-------------------

Use ``@`` to select attributes:

.. code-block:: sql

   -- Get the 'id' attribute
   SELECT xml_extract_text(xml, '//item/@id');

   -- Get all attributes of <item> elements
   SELECT xml_extract_attributes(xml, '//item');

Predicates
----------

Use ``[...]`` to filter results:

Position Predicates
~~~~~~~~~~~~~~~~~~~

.. code-block:: sql

   -- First element
   SELECT xml_extract_text(xml, '//item[1]');

   -- Last element
   SELECT xml_extract_text(xml, '//item[last()]');

   -- First three elements
   SELECT xml_extract_text(xml, '//item[position() <= 3]');

Attribute Predicates
~~~~~~~~~~~~~~~~~~~~

.. code-block:: sql

   -- Elements with specific attribute value
   SELECT xml_extract_text(xml, '//book[@category="fiction"]/title');

   -- Elements that have an attribute
   SELECT xml_extract_text(xml, '//item[@id]');

   -- Numeric comparison
   SELECT xml_extract_text(xml, '//product[@price > 100]/name');

Text Predicates
~~~~~~~~~~~~~~~

.. code-block:: sql

   -- Elements containing specific text
   SELECT xml_extract_text(xml, '//item[contains(text(), "sale")]');

   -- Elements starting with text
   SELECT xml_extract_text(xml, '//item[starts-with(text(), "New")]');

Namespace Handling
------------------

For namespaced documents, simple paths may not work:

.. code-block:: sql

   -- This might return empty for namespaced XML:
   SELECT xml_extract_text(xml, '//element');

Use ``local-name()`` to match regardless of namespace:

.. code-block:: sql

   -- Match any element with local name 'element'
   SELECT xml_extract_text(xml, '//*[local-name()="element"]');

   -- With predicates
   SELECT xml_extract_text(xml, '//*[local-name()="item" and @id="123"]');

   -- Nested elements
   SELECT xml_extract_text(xml, '//*[local-name()="person"]/*[local-name()="name"]');

.. note::

   The ``read_xml()`` function automatically strips namespaces during schema inference, so column names won't include namespace prefixes. However, XPath queries against raw XML documents require namespace handling.

Common Functions
----------------

XPath 1.0 supports these functions:

String Functions
~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Function
     - Description
   * - ``text()``
     - Select text content
   * - ``contains(str, substr)``
     - Check if string contains substring
   * - ``starts-with(str, prefix)``
     - Check if string starts with prefix
   * - ``string-length(str)``
     - Get string length
   * - ``concat(s1, s2, ...)``
     - Concatenate strings
   * - ``normalize-space(str)``
     - Normalize whitespace

Numeric Functions
~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Function
     - Description
   * - ``sum(nodeset)``
     - Sum of numeric values
   * - ``count(nodeset)``
     - Count nodes
   * - ``floor(num)``
     - Round down
   * - ``ceiling(num)``
     - Round up

Boolean Functions
~~~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Function
     - Description
   * - ``not(expr)``
     - Boolean negation
   * - ``true()``
     - Returns true
   * - ``false()``
     - Returns false

Node Functions
~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Function
     - Description
   * - ``local-name()``
     - Element name without namespace
   * - ``name()``
     - Element name with namespace prefix
   * - ``namespace-uri()``
     - Namespace URI
   * - ``last()``
     - Last position in context
   * - ``position()``
     - Current position

Examples
--------

Complex Queries
~~~~~~~~~~~~~~~

.. code-block:: sql

   -- Products over $100 in electronics category
   SELECT xml_extract_text(xml,
       '//product[@category="electronics" and @price > 100]/name'
   );

   -- Second author of first book
   SELECT xml_extract_text(xml, '//book[1]/author[2]');

   -- All items with 'sale' in description
   SELECT xml_extract_text(xml,
       '//item[contains(description, "sale")]/name'
   );

   -- Count of items
   SELECT xml_extract_text(xml, 'count(//item)');

HTML-Specific Patterns
~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: sql

   -- All paragraph text
   SELECT html_extract_text(html, '//p');

   -- Links in navigation
   SELECT html_extract_text(html, '//nav//a/@href');

   -- Images with alt text
   SELECT html_extract_text(html, '//img[@alt]/@src');

   -- Table cells in first row
   SELECT html_extract_text(html, '//table//tr[1]//td');
