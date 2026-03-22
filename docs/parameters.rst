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
   * - ``force_list``
     - VARCHAR[]
     - []
     - Column names that should always be LIST type, even if they appear only once
   * - ``nullstr``
     - VARCHAR or VARCHAR[]
     - (none)
     - String value(s) to interpret as NULL. Excluded from type inference and converted to NULL during extraction. Case-sensitive.

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
