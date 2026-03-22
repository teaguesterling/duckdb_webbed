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
     - Maximum file size in bytes (16MB default)
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
   * - ``datetime_format``
     - VARCHAR or VARCHAR[]
     - 'auto'
     - Controls date/time detection and parsing. Accepts a format string (``'%m/%d/%Y'``), a preset name, a list of formats, or ``'none'`` to disable. See :ref:`datetime-format` below.
   * - ``force_list``
     - VARCHAR[]
     - []
     - Column names that should always be LIST type, even if they appear only once

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
     - 'prefix'
     - How to handle attributes: ``'prefix'`` adds prefix to column names, ``'merge'`` merges with elements, ``'ignore'`` ignores attributes
   * - ``attr_prefix``
     - VARCHAR
     - '@'
     - Prefix added to attribute column names when ``attr_mode='prefix'``
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
     - 'object'
     - How to handle empty elements: ``'object'`` returns empty struct, ``'null'`` returns NULL, ``'string'`` returns empty string

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

.. note::

   When auto-detecting, ambiguous date formats (e.g., ``03/04/2024``) default to US (month-first) ordering, consistent with DuckDB conventions. Use ``datetime_format='eu'`` to override.

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

Attribute Handling
~~~~~~~~~~~~~~~~~~

.. code-block:: sql

   -- Use underscore prefix for attributes
   SELECT * FROM read_xml('data.xml', attr_prefix := '_');

   -- Ignore attributes entirely
   SELECT * FROM read_xml('data.xml', attr_mode := 'ignore');

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
