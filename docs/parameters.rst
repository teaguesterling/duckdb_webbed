Parameters Reference
====================

This page provides a comprehensive reference for all parameters available in the file reading functions.

Common Parameters
-----------------

These parameters are available for both ``read_xml`` and ``read_html``:

.. list-table::
   :header-rows: 1
   :widths: 20 15 15 50

   * - Parameter
     - Type
     - Default
     - Description
   * - ``filename``
     - BOOLEAN
     - false
     - Include a ``filename`` column in output with the source file path
   * - ``ignore_errors``
     - BOOLEAN
     - false
     - Skip files that fail to parse instead of raising an error
   * - ``maximum_file_size``
     - BIGINT
     - 16777216
     - Maximum file size in bytes for DOM parsing (16MB default). Files above this use SAX streaming when ``streaming=true``. For a compressed file this is the decompressed size.
   * - ``auto_detect``
     - BOOLEAN
     - true
     - Enable automatic schema detection and type inference

Schema Inference Parameters
---------------------------

.. list-table::
   :header-rows: 1
   :widths: 20 15 15 50

   * - Parameter
     - Type
     - Default
     - Description
   * - ``record_element``
     - VARCHAR
     - NULL
     - XPath or tag name identifying which elements become table rows. If not specified, immediate children of root become rows.
   * - ``root_element``
     - VARCHAR
     - NULL
     - Specify the XML root element name for schema inference
   * - ``columns``
     - VARCHAR[]
     - NULL
     - Pre-specify expected column names for better performance
   * - ``max_depth``
     - INTEGER
     - 10
     - Maximum nesting depth to parse (-1 for unlimited, capped at 10)
   * - ``unnest_as``
     - VARCHAR
     - 'struct'
     - How to handle nested elements: ``'columns'`` for flattening, ``'struct'`` for preservation

Type Handling Parameters
------------------------

.. list-table::
   :header-rows: 1
   :widths: 20 15 15 50

   * - Parameter
     - Type
     - Default
     - Description
   * - ``all_varchar``
     - BOOLEAN
     - false
     - Force all scalar types to VARCHAR. Preserves nested structure (STRUCT, LIST) but converts leaf values.
   * - ``auto_type_candidates``
     - VARCHAR[]
     - (every type)
     - Scalar types inference may choose. VARCHAR is always a candidate. See :ref:`auto-type-candidates` below.
   * - ``numeric_inference``
     - VARCHAR
     - 'exact'
     - ``'exact'`` types a column numeric only when the type gives back the exact text of every value, so ``1.10`` stays VARCHAR. ``'lenient'`` types any text DuckDB's cast reads, so ``1.10`` is DOUBLE ``1.1``. See :doc:`schema_inference`.
   * - ``datetime_format``
     - VARCHAR or VARCHAR[]
     - 'auto'
     - Controls date/time detection and parsing. Accepts a format string (``'%m/%d/%Y'``), a preset name, a list of formats, or ``'none'`` to disable. See :ref:`datetime-format` below.
   * - ``force_list``
     - VARCHAR[]
     - []
     - Elements that should always be LIST type, even if they appear only once. A bare name (``'tag'``) or ``'//tag'`` matches the element at any depth. A path such as ``'sections/columns/items'`` matches only that chain of child elements under the record element. ``'//columns/items'`` matches the chain at any depth. See :ref:`force-list-paths`.
   * - ``nullstr``
     - VARCHAR or VARCHAR[]
     - (none)
     - String value(s) to interpret as NULL. Excluded from type inference and converted to NULL during extraction. Case-sensitive.
   * - ``streaming``
     - BOOLEAN
     - true
     - Enable SAX-based streaming for files exceeding ``maximum_file_size``. When true (default), oversized files are parsed using SAX instead of raising an error. SAX processes XML as a stream without building a DOM tree, reducing peak memory to proportional to a single record. A single record may span at most a quarter of ``memory_limit``, since its tree takes about three times its size outside the buffer manager. The record's start tag and its entity references, counted at their expanded size, are part of that span, and no single element may be buffered past it. Schema inference ends its sample once the sampled records reach the same size, so a field first seen after that point needs ``columns``. Set to false to error on oversized files (original behavior). Only supports simple tag names for ``record_element``. Not available for HTML.
   * - ``preserve_whitespace``
     - BOOLEAN
     - true
     - When true (v2.0.0 default), trims leading/trailing whitespace and normalizes CRLF/CR to LF per XML 1.0 §2.11 but preserves internal whitespace (newlines, tabs, multi-space runs). When false, collapses all internal whitespace runs to a single space (v1.x behavior).

Attribute Handling Parameters
-----------------------------

.. list-table::
   :header-rows: 1
   :widths: 20 15 15 50

   * - Parameter
     - Type
     - Default
     - Description
   * - ``attr_mode``
     - VARCHAR
     - 'columns'
     - How to handle attributes: ``'columns'`` makes each attribute a column under its bare name, ``'prefixed'`` prepends ``attr_prefix`` to attribute column names, ``'discard'`` drops attributes
   * - ``attr_prefix``
     - VARCHAR
     - '@'
     - Prefix added to attribute column names when ``attr_mode='prefixed'``
   * - ``text_key``
     - VARCHAR
     - '#text'
     - Key name for text content when elements have mixed content

Namespace Parameters
--------------------

.. list-table::
   :header-rows: 1
   :widths: 20 15 15 50

   * - Parameter
     - Type
     - Default
     - Description
   * - ``namespaces``
     - VARCHAR
     - 'strip'
     - Namespace handling mode: ``'strip'`` removes prefixes, ``'expand'`` uses full URIs, ``'keep'`` preserves prefixes

Empty Element Handling
----------------------

.. list-table::
   :header-rows: 1
   :widths: 20 15 15 50

   * - Parameter
     - Type
     - Default
     - Description
   * - ``empty_elements``
     - VARCHAR
     - 'null'
     - How an empty element (no text, children or attributes) affects type inference. With ``'null'`` or ``'object'``, the column takes its type from the element's other occurrences, in the same file or in other files, and is VARCHAR only when the element is empty everywhere. ``'string'`` types the column VARCHAR whenever the element is empty somewhere. The empty element itself reads as NULL, or as a struct of NULL fields in a STRUCT column

Compressed Files
----------------

``read_xml``, ``read_html``, ``read_xml_objects``, and ``read_html_objects`` decompress files ending in ``.gz``, and ``.zst`` once the ``parquet`` extension is loaded. Compressed and plain files can mix in one glob.

.. code-block:: sql

   SELECT * FROM read_xml('dumps/*.xml.gz', record_element := 'record');

A compressed file's decompressed size isn't known before it is read, so the reader decompresses up to ``maximum_file_size`` to decide between DOM and SAX. A file that overflows the limit continues through SAX from the bytes already decompressed.

``compression`` overrides detection by name: ``'auto'`` (the default), ``'none'`` to read a file's bytes as they are, ``'gzip'``, or ``'zstd'``. A name DuckDB doesn't recognize fails when a file is opened.

.. code-block:: sql

   SELECT * FROM read_xml('legacy/feed.xml.gz', compression := 'none');

Multi-File Parameters
---------------------

.. list-table::
   :header-rows: 1
   :widths: 20 15 15 50

   * - Parameter
     - Type
     - Default
     - Description
   * - ``union_by_name``
     - BOOLEAN
     - false
     - Combine columns by name when reading multiple files with different schemas

Schema inference over several files (``union_by_name`` or ``sample_files``) parses the files in parallel at bind time. Each file being inferred holds its parsed document in memory, outside DuckDB's buffer manager, so peak memory grows with the number parsed at once. By default that number is DuckDB's ``threads``, lowered so that 32 times ``maximum_file_size`` per file fits in ``memory_limit``. The ``xml_inference_threads`` setting replaces that default:

.. code-block:: sql

   -- Parse one file at a time during inference: lowest memory, slowest bind
   SET xml_inference_threads = 1;

   -- Back to the default, bounded by threads and memory_limit
   RESET xml_inference_threads;

The cap changes neither the inferred schema nor which error a failing bind raises. Files are merged in list order either way.


.. _datetime-format:

Datetime Format Parameter
-------------------------

The ``datetime_format`` parameter controls how date, time, and timestamp values are detected and parsed.

**Preset names:**

.. list-table::
   :header-rows: 1
   :widths: 20 35 20

   * - Preset
     - Format
     - Target Type
   * - ``'auto'``
     - Built-in candidate list (default)
     - mixed
   * - ``'none'``
     - Disables temporal detection
     - \-
   * - ``'us'``
     - ``%m/%d/%Y``
     - DATE
   * - ``'eu'``
     - ``%d/%m/%Y``
     - DATE
   * - ``'iso'``
     - ``%Y-%m-%d``
     - DATE
   * - ``'us_timestamp'``
     - ``%m/%d/%Y %I:%M:%S %p``
     - TIMESTAMP
   * - ``'eu_timestamp'``
     - ``%d/%m/%Y %H:%M:%S``
     - TIMESTAMP
   * - ``'iso_timestamp'``
     - ``%Y-%m-%dT%H:%M:%S``
     - TIMESTAMP
   * - ``'iso_timestamptz'``
     - ``%Y-%m-%dT%H:%M:%S%z``
     - TIMESTAMPTZ
   * - ``'12hour'``
     - ``%I:%M:%S %p``
     - TIME
   * - ``'24hour'``
     - ``%H:%M:%S``
     - TIME

**How it works:**

When ``datetime_format='auto'`` (the default), the extension tries a built-in list of common formats against sample values. Formats that fail to parse any sample are eliminated. The first surviving format determines the column type.

When an explicit format or preset is specified, only those formats are tried. If no format matches all samples, the column falls back to VARCHAR.

When no one format reads every sample but each is a date or timestamp, the column is TIMESTAMP, as in ``read_csv``. This covers timestamps in two formats, such as ``2024-01-01T10:00:00`` beside ``2024-01-02 10:00:00``. A date-only sample keeps the column VARCHAR while ``DATE`` is a candidate, as in ``read_csv``. With ``DATE`` excluded from ``auto_type_candidates``, a date reads as midnight. This needs each sample to be one that a date or timestamp format reads and DuckDB's cast reads too. Such a column keeps no format: DuckDB's cast reads its values and drops a UTC offset, as ``read_csv`` does. A union of files whose formats differ, such as a date file and a timestamp file, is TIMESTAMP in the same way, and it also drops a UTC offset in a value past the sample. One file read with its own format raises on such a value instead.

.. note::

   When auto-detecting, an ambiguous date (e.g., ``03/04/2024``) resolves to US (month-first)
   ordering for a **top-level column**, consistent with DuckDB conventions. The column's format
   is settled by eliminating candidates across all of its sampled values, so a single
   unambiguous day-first value elsewhere in the column will make the whole column day-first.

   Inside a nested STRUCT field or a repeated (LIST) element, no per-field format is recorded,
   so extraction cannot repeat that elimination from one value. There an ambiguous date is
   **declined** rather than guessed: it surfaces as a conversion error, or NULL under
   ``ignore_errors``. Guessing would read ``01/02/2024`` as 2 January while the same document's
   top-level column reads it as 1 February. Unambiguous nested values parse normally.

   Naming a format removes the ambiguity entirely -- the presets resolve to a single candidate
   (``'us'`` is ``%m/%d/%Y``, ``'eu'`` is ``%d/%m/%Y``).

**Explicit columns:**

A column declared through ``columns`` has no recorded format. Its values are read with DuckDB's strict casts, then with the candidate list above. An explicit ``TIME`` column reads ``01:30:00 PM`` as ``13:30:00``, as inference does. The marker needs a space before it: ``01:30:00PM`` matches no candidate, so it raises, or reads NULL under ``ignore_errors``. A zero UTC offset (``Z``, ``UTC``, ``+00:00``) reads as the bare time. A non-zero offset such as ``+05:00`` raises instead of being dropped. Declare the column ``TIMETZ`` to keep it. A ``TIMESTAMPTZ`` column applies a numeric offset, as DuckDB's cast does: ``2024-01-01 10:00:00+05`` reads as 05:00 UTC.

**Interactions with other parameters:**

- ``all_varchar=true`` overrides ``datetime_format`` — no temporal detection occurs.
- ``datetime_format='none'`` disables all temporal detection.
- An explicit ``datetime_format`` overrides ``temporal_detection=false`` if both are set.

.. code-block:: sql

   -- Parse US-format dates
   SELECT * FROM read_xml('data.xml', datetime_format='us');

   -- Parse European dates
   SELECT * FROM read_xml('data.xml', datetime_format='eu');

   -- Disable date detection
   SELECT * FROM read_xml('data.xml', datetime_format='none');

   -- Multiple formats (first surviving format wins)
   SELECT * FROM read_xml('data.xml', datetime_format=['%m/%d/%Y', '%Y-%m-%d %H:%M:%S']);

   -- Custom format string
   SELECT * FROM read_xml('data.xml', datetime_format='%Y/%m/%d');

.. _auto-type-candidates:

auto_type_candidates
~~~~~~~~~~~~~~~~~~~~

``auto_type_candidates`` restricts the scalar types inference may choose. It mirrors
``read_csv``'s option of the same name: a list of type names, with VARCHAR always a candidate
whether listed or not. A value no candidate can read stays VARCHAR.

Use it when a source's text is identity-bearing. In Salesforce metadata, ``versionNumber`` and
``apiVersion`` look like decimals. Inference already keeps ``1.10`` VARCHAR, because DOUBLE would
not reproduce its spelling, but ``58.0`` reads back exactly and becomes DOUBLE. Excluding DOUBLE
keeps every such field VARCHAR without naming the fields:

.. code-block:: sql

   SELECT * FROM read_xml('metadata/*.xml', auto_type_candidates := ['BIGINT', 'BOOLEAN', 'DATE']);

A value takes the narrowest allowed type that reads it. An integer becomes INTEGER, or BIGINT
when INTEGER is excluded, or DOUBLE when both are. An integer never becomes BOOLEAN, so ``0`` and
``1`` stay VARCHAR under ``['BOOLEAN']``, as in ``read_csv``. A date becomes TIMESTAMP at midnight
when DATE is excluded. A ``union_by_name`` merge of per-file types never widens past the
candidates. Nested STRUCT and LIST shapes are unchanged, and only their scalar leaves and
attributes are restricted.

**Accepted types:** VARCHAR, BOOLEAN, INTEGER, BIGINT, DOUBLE, DATE, TIME, TIMETZ, TIMESTAMP and
TIMESTAMPTZ, by any name DuckDB resolves to them (``'int8'`` is BIGINT). An unknown type name, an
empty list, or a NULL element is an error, as in ``read_csv``. JSON and XML are VARCHAR under
another name, and inference never produces them, so naming either is an error.

**Differences from read_csv:**

- ``read_csv`` also accepts TINYINT, SMALLINT, HUGEINT, FLOAT, DECIMAL and BIGNUM, and honours
  each. Inference here never produces them, so naming one is an error.
- ``read_csv`` widens a zoneless date or timestamp to TIMESTAMPTZ, read in the session time zone.
  Webbed reads zoneless text in a TIMESTAMPTZ column as UTC, so the two would disagree on the
  instant. Here zoneless text stays VARCHAR under ``['TIMESTAMPTZ']``, and only text carrying an
  offset becomes TIMESTAMPTZ. A ``union_by_name`` merge of a zoneless file with a zoned one is
  VARCHAR for the same reason, with or without this option.
- ``read_csv`` ignores the option alongside ``columns`` or ``all_varchar``. Here either pairing is
  an error, as ``columns`` with ``all_varchar`` already is. ``all_varchar := true`` is
  ``auto_type_candidates := ['VARCHAR']``.

**Interactions with other parameters:**

- ``datetime_format`` supplies the formats temporal detection tries. A date format yields TIMESTAMP
  when only TIMESTAMP is allowed. Any other format whose type is not a candidate is skipped, and the
  value falls through to the numeric and boolean checks.
- ``union_by_name`` can give a date file and a timestamp file the same TIMESTAMP type. When files
  agree on a temporal type through different formats, the column reads with DuckDB's default
  cast, which accepts ISO dates and timestamps. Non-ISO formats that disagree across files raise
  a conversion error.
- ``nullstr`` matches are excluded from inference as usual.

Examples
--------

Basic Usage
~~~~~~~~~~~

.. code-block:: sql

   -- Include filenames
   SELECT * FROM read_xml('*.xml', filename=true);

   -- Skip problematic files
   SELECT * FROM read_xml('*.xml', ignore_errors=true);

   -- Limit file size
   SELECT * FROM read_xml('*.xml', maximum_file_size=1048576);  -- 1MB

Schema Control
~~~~~~~~~~~~~~

.. code-block:: sql

   -- Extract specific records
   SELECT * FROM read_xml('feed.xml', record_element := 'item');

   -- Force array types
   SELECT * FROM read_xml('data.xml', force_list := ['tag', 'category']);

   -- Preserve all values as strings
   SELECT * FROM read_xml('data.xml', all_varchar := true);

   -- Infer integers, booleans and dates, but never DOUBLE
   SELECT * FROM read_xml('data.xml', auto_type_candidates := ['BIGINT', 'BOOLEAN', 'DATE']);

Attribute Handling
~~~~~~~~~~~~~~~~~~

.. code-block:: sql

   -- Prefix attribute columns with an underscore
   SELECT * FROM read_xml('data.xml', attr_mode := 'prefixed', attr_prefix := '_');

   -- Drop attributes entirely
   SELECT * FROM read_xml('data.xml', attr_mode := 'discard');

Multi-File Processing
~~~~~~~~~~~~~~~~~~~~~

.. code-block:: sql

   -- Combine files with different schemas
   SELECT * FROM read_xml('config/*.xml', union_by_name := true);

   -- Process with error tolerance
   SELECT * FROM read_xml('data/*.xml',
       ignore_errors := true,
       union_by_name := true,
       filename := true
   );
