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

.. note::

   A tag name, or ``//tag``, matches the outermost elements with that name at any depth. An ``<item>`` inside
   another ``<item>`` is not a row of its own: it stays in its parent's column. Use an XPath expression to select
   the inner elements as rows:

   .. code-block:: sql

      SELECT * FROM read_xml('data.xml', record_element := '//attachmentSet/item');

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

- ``id`` (from attribute)
- ``category`` (from attribute)
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

An explicit typed list column also reads an element that wraps its items. With
``columns := {'items': 'INTEGER[]'}``, ``<items><i>1</i><i>2</i></items>`` reads ``[1, 2]``, and
repeated wrappers contribute their items in document order. An element with text of its own
beside its children stays one item. A ``VARCHAR[]`` column keeps a wrapper as its serialized
XML, one item per wrapper.

Use ``force_list`` to ensure array types even for single occurrences:

.. code-block:: sql

   SELECT * FROM read_xml('orders.xml', force_list := ['item']);

.. _force-list-paths:

Forcing nested elements to lists
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Each ``force_list`` entry is an element name or a path of element names separated by ``/``:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Entry
     - Matches
   * - ``'item'`` or ``'//item'``
     - Every ``item`` element, at any depth below the record element
   * - ``'sections/columns'`` or ``'./sections/columns'``
     - ``columns`` elements whose parent is a ``sections`` child of the record element
   * - ``'//columns/items'``
     - ``items`` elements whose parent is a ``columns`` element, at any depth
   * - ``'//Layout/sections'``
     - ``sections`` children of a ``Layout`` element, which may be the record element

A path is relative to the record element, like an XPath child path. It pins one location in
the tree, so an element with the same name elsewhere keeps its inferred type. A step matches
inside every instance of its parent, so a path reaches through a parent that is itself a list.
A forced element that is missing from a record reads as NULL, at the top level and nested alike.

.. code-block:: sql

   -- <Layout><layoutSections><layoutColumns><layoutItems>...</layoutItems></layoutColumns></layoutSections></Layout>
   SELECT * FROM read_xml('layouts.xml',
       record_element := 'Layout',
       force_list := ['layoutSections',
                      'layoutSections/layoutColumns',
                      'layoutSections/layoutColumns/layoutItems']);
   -- layoutSections STRUCT(..., layoutColumns STRUCT(layoutItems STRUCT(...)[])[])[]

XML names can't contain ``/``, so steps need no escaping. A step may hold any other name
character, such as ``.``, ``-``, or non-ASCII letters. Matching is case-sensitive. A namespace-
prefixed step matches the element name as written or by its local name, since element names
keep their prefix in some modes and lose it in others.

Each step must be an XML name. Predicates (``[...]``), attributes (``@``), wildcards (``*``),
axes (``child::``), ``..``, ``//`` within a path, and absolute paths (``/Layout/...``) are
rejected with an error. Empty entries are ignored.

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
     - true/false, yes/no, on/off
     - ``true``, ``false``, ``yes``, ``no``, ``on``, ``off``
   * - INTEGER/BIGINT
     - Whole numbers, written the way DuckDB prints them
     - ``42``, ``-100``, ``999999``
   * - DOUBLE
     - Decimal numbers in positional notation, whose digits a DOUBLE gives back exactly
     - ``3.14``, ``-0.5``, ``1.0``, ``0.00001``
   * - DATE
     - Date formats (ISO, US, EU)
     - ``2024-01-15``, ``03/15/2024``
   * - TIMESTAMP
     - Date with time
     - ``2024-01-15T10:30:00``
   * - TIMESTAMPTZ
     - Date with time and timezone
     - ``2024-01-15T10:30:00+05:00``
   * - TIME
     - Time of day
     - ``10:30:00``, ``02:30:00 PM``
   * - VARCHAR
     - Everything else
     - ``hello``, ``mixed123``
   * - STRUCT
     - Nested elements
     - ``<address><city>NYC</city></address>``
   * - LIST
     - Repeated elements
     - Multiple ``<tag>`` elements

Temporal type detection uses DuckDB's ``StrpTimeFormat`` with a candidate elimination approach.
A list of format candidates is tested against all sample values; candidates that fail on any
sample are eliminated. The first surviving candidate determines the column type. By default,
auto-detection prioritizes ISO formats, then US, then EU for ambiguous dates.

Elimination runs per column, across every sample. Nested STRUCT fields and repeated (LIST)
elements are typed the same way, but no per-field format is recorded — so when extracting an
*ambiguous* nested value the reader declines rather than guessing. See the note in
:doc:`parameters`.

Numbers and booleans are detected from the text itself, so the answer has to be a type every
sampled lexeme can be read as. ``0`` and ``1`` are read as integers, not booleans, when they
make up the column; and a column mixing a boolean lexeme with a numeric one has no common
lexical type, so it widens to VARCHAR and keeps both spellings.

A numeric type is chosen only when it can give back the exact text of every sample, because
otherwise the original spelling is lost and no cast can restore it. These stay VARCHAR:

.. list-table::
   :header-rows: 1
   :widths: 40 30 30

   * - Lexeme class
     - Example
     - A numeric type would read
   * - Leading zeros
     - ``007``
     - ``7``
   * - Trailing fractional zeros
     - ``1.10``
     - ``1.1``
   * - Exponent notation
     - ``1e3``
     - ``1000.0``
   * - Sign, hex, digit separator
     - ``+5``, ``0x10``, ``1_000``
     - ``5``, ``16``, ``1000``
   * - Bare decimal point
     - ``.5``, ``5.``
     - ``0.5``, ``5.0``
   * - Negative zero integer
     - ``-0``
     - ``0``
   * - More digits than a DOUBLE holds
     - ``3.14159265358979323846``
     - ``3.141592653589793``

Element text is trimmed before typing, for VARCHAR columns too, so ``<v> 5 </v>`` is the lexeme
``5``. Attribute values keep their whitespace, so ``v=" 5 "`` stays VARCHAR.

A column of integers and decimals still types DOUBLE, so ``1`` and ``2.5`` read as ``1.0`` and
``2.5``. The integer is exact in the double, so ``CAST(v AS BIGINT)`` recovers ``1``. An integer
the double cannot hold exactly, such as ``9007199254740993`` next to ``2.5``, keeps the column
VARCHAR. Under ``union_by_name`` the same holds across files: an integer file beside a decimal
file is DOUBLE when every sampled integer is exact in a double, and VARCHAR otherwise.

The same rule applies to values outside the sample. A value such as ``007`` that reaches an
inferred INTEGER column gets the "does not match column type" error, or NULL under
``ignore_errors``, as any other non-conforming value does (see "Out-of-Sample Values" below).
Types given with ``columns`` are casts the caller asked for and are not checked this way:
``columns := {'v': 'INTEGER'}`` still reads ``007`` as ``7``.

Under ``ignore_errors`` the value reads as NULL with no error. For example, ``0.50`` past the
sample of a DOUBLE column reads as NULL, although DuckDB's cast reads it as ``0.5``.

``numeric_inference := 'lenient'`` turns the rule off. A numeric type is then chosen for any
text DuckDB's cast reads, as in v2.14.1 and earlier: ``1.10`` is DOUBLE ``1.1``, and ``0.50`` past the
sample reads as ``0.5``. Under ``union_by_name``, an integer file beside a decimal file is
DOUBLE. The default is ``'exact'``. Use ``'lenient'`` when the numbers matter and their spelling
does not, such as floats written by another program.

Use the ``datetime_format`` parameter to control this behavior — see :doc:`parameters` for details.

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

SAX Streaming and Schema Inference
-----------------------------------

When files exceed ``maximum_file_size`` (16MB by default), the extension uses SAX-based
streaming instead of building a full DOM tree. This affects schema inference in two ways:

1. **Sample-based inference** — SAX mode reads the first ``sample_size`` records (default: 10240),
   buffers them to build a synthetic document that the normal inference logic types, then streams
   the rest for extraction. The schema is not revised after the sample window, so columns or types
   that only appear beyond ``sample_size`` records may not be detected. Because the sampled records
   are held in memory, a very large ``sample_size`` raises memory use in SAX mode roughly in
   proportion to the prefix size — the 10240 default is a finite cap chosen to balance detection
   accuracy against that cost.

   The DOM path (files under ``maximum_file_size``) instead applies ``sample_size`` as a cap on the
   number of sampled *values per field*. There, ``sample_size := -1`` samples every value for
   always-correct detection at the cost of scanning the whole input. In SAX mode ``sample_size <= 0``
   triggers a second, memory-bounded pass: after typing from the first 50 records it streams the
   whole file and widens to ``VARCHAR`` any column, or any STRUCT or LIST leaf, whose later values
   do not fit the sampled type (see "Out-of-Sample Values" below). This is the SAX equivalent of the DOM path's "sample every
   value" — records are tested and discarded per chunk, so detection is complete without buffering
   the whole prefix in memory, at the cost of one extra read of the file.

2. **Simple ``record_element`` only** — SAX mode matches record elements by simple tag name
   (e.g., ``'item'``). XPath expressions like ``'//ns:item[@type="active"]'`` or path-based
   patterns like ``'/root/data/item'`` require DOM parsing. When the file is too large for DOM
   and the ``record_element`` contains XPath syntax, the extension raises an error.

.. code-block:: sql

   -- Works in SAX mode (simple tag name)
   SELECT * FROM read_xml('huge.xml', record_element := 'item');

   -- Falls back to DOM (XPath expression)
   SELECT * FROM read_xml('data.xml', record_element := '//item[@status="active"]');

Set ``streaming := false`` to force DOM mode for any file (will error if the file exceeds
``maximum_file_size``).

Out-of-Sample Values
--------------------

Schema inference types each column from a sample of the input, then casts every value to that
type during extraction. A value that appears **beyond the sample window** and does not fit the
inferred type, including one the type would rewrite such as ``007`` or ``1.10``, raises an error
such as::

   Invalid Input Error: read_xml: value '24 495,40 Kč' does not match column type BIGINT,
   which was inferred from the first N sampled values.

This is most common on **large files**, where the sample necessarily covers only a prefix. Two
ways to handle it:

* ``all_varchar := true`` — skip type inference entirely; every scalar is read as ``VARCHAR`` and
  no value can be rejected. This is the most robust choice for a large file of uncertain or mixed
  data, and it works in both DOM and SAX mode.

  .. code-block:: sql

     SELECT * FROM read_xml('huge.xml', record_element := 'item', all_varchar := true);

* **Widen the detection window** so the sniffer sees the outlier:

  .. code-block:: sql

     -- DOM path (files under maximum_file_size): sample every value, always-correct
     -- detection at the cost of scanning the whole input.
     SELECT * FROM read_xml('data.xml', sample_size := -1);

     -- SAX path (large files): sample_size <= 0 runs a full-file verify pass that widens any
     -- column with an out-of-sample value to VARCHAR, preserving the value. Records are tested
     -- and discarded per chunk, so memory stays O(1) in the file size -- unlike raising
     -- sample_size to a large finite value, which buffers that many records. Costs one extra
     -- read of the file at bind.
     SELECT * FROM read_xml('huge.xml', record_element := 'item', sample_size := -1);

``ignore_errors := true`` is a third option: it replaces each non-conforming value with ``NULL``
and keeps the row, rather than erroring or preserving the original text.

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
