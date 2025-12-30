Schema Inference
================

The webbed extension automatically infers DuckDB table schemas from XML and HTML documents using a 3-phase deterministic approach.

Overview
--------

When you read an XML file without specifying a schema, the extension:

1. **Identifies Records** - Determines which XML elements represent table rows
2. **Identifies Columns** - Analyzes child elements and attributes to create columns
3. **Infers Types** - Determines appropriate DuckDB types for each column

Phase 1: Identify Records
-------------------------

By default, immediate children of the root element become rows:

.. code-block:: xml

   <catalog>
     <product>...</product>  <!-- Row 1 -->
     <product>...</product>  <!-- Row 2 -->
     <product>...</product>  <!-- Row 3 -->
   </catalog>

You can customize this with the ``record_element`` parameter:

.. code-block:: sql

   -- Extract nested items as rows
   SELECT * FROM read_xml('feed.xml', record_element := 'item');

   -- Use XPath syntax
   SELECT * FROM read_xml('data.xml', record_element := '//entry');

.. warning::

   ``record_element`` matches at ALL depths. If your document has ``<item>`` elements at multiple levels, all will become rows. Filter by column presence if needed:

   .. code-block:: sql

      SELECT * FROM read_xml('data.xml', record_element := 'item')
      WHERE specific_column IS NOT NULL;

Phase 2: Identify Columns
-------------------------

For each record element, the extension creates columns from:

1. **Attributes** on the record element
2. **Child elements** of the record

.. code-block:: xml

   <product id="123" category="electronics">
     <name>Widget</name>
     <price>29.99</price>
     <tags>
       <tag>new</tag>
       <tag>sale</tag>
     </tags>
   </product>

Results in columns:

- ``@id`` (from attribute)
- ``@category`` (from attribute)
- ``name`` (from child element)
- ``price`` (from child element)
- ``tags`` (nested STRUCT with LIST)

Repeated Elements
~~~~~~~~~~~~~~~~~

When an element name repeats within a record, it becomes a LIST:

.. code-block:: xml

   <order>
     <item>Product A</item>
     <item>Product B</item>
     <item>Product C</item>
   </order>

The ``item`` column will be ``LIST<VARCHAR>``.

Use ``force_list`` to ensure array types even for single occurrences:

.. code-block:: sql

   SELECT * FROM read_xml('orders.xml', force_list := ['item']);

Phase 3: Infer Types
--------------------

The extension automatically detects these types:

.. list-table::
   :header-rows: 1
   :widths: 25 35 40

   * - DuckDB Type
     - Detection Pattern
     - Examples
   * - BOOLEAN
     - true/false, 1/0
     - ``true``, ``false``, ``1``, ``0``
   * - INTEGER/BIGINT
     - Whole numbers
     - ``42``, ``-100``, ``999999``
   * - DOUBLE
     - Decimal numbers
     - ``3.14``, ``-0.5``, ``1.0e10``
   * - DATE
     - ISO 8601 dates
     - ``2024-01-15``
   * - TIMESTAMP
     - ISO 8601 with time
     - ``2024-01-15T10:30:00Z``
   * - VARCHAR
     - Everything else
     - ``hello``, ``mixed123``
   * - STRUCT
     - Nested elements
     - ``<address><city>NYC</city></address>``
   * - LIST
     - Repeated elements
     - Multiple ``<tag>`` elements

Forcing VARCHAR Types
~~~~~~~~~~~~~~~~~~~~~

Use ``all_varchar`` to prevent type inference issues:

.. code-block:: sql

   -- All scalar values become VARCHAR
   SELECT * FROM read_xml('data.xml', all_varchar := true);

This preserves nested structure (STRUCT, LIST) but converts leaf values:

- ``STRUCT(a INT, b FLOAT)`` → ``STRUCT(a VARCHAR, b VARCHAR)``
- ``LIST<INTEGER>`` → ``LIST<VARCHAR>``

Controlling Depth
-----------------

Limit parsing depth with ``max_depth``:

.. code-block:: sql

   -- Only parse 3 levels deep
   SELECT * FROM read_xml('deep.xml', max_depth := 3);

   -- Unlimited (capped at 10 for safety)
   SELECT * FROM read_xml('deep.xml', max_depth := -1);

Common Patterns
---------------

RSS Feeds
~~~~~~~~~

.. code-block:: sql

   -- Default: Returns channel metadata
   SELECT * FROM read_xml('feed.rss');

   -- Extract individual items
   SELECT * FROM read_xml('feed.rss', record_element := 'item');

AWS API Responses
~~~~~~~~~~~~~~~~~

.. code-block:: sql

   -- Extract EC2 volumes
   SELECT * FROM read_xml('volumes.xml', record_element := 'item');

Configuration Files
~~~~~~~~~~~~~~~~~~~

.. code-block:: sql

   -- Preserve structure
   SELECT * FROM read_xml('config.xml', unnest_as := 'struct');

   -- Flatten to columns
   SELECT * FROM read_xml('config.xml', unnest_as := 'columns');
