Contributing
============

Contributions to the webbed extension are welcome! This guide will help you get started.

Development Setup
-----------------

Prerequisites
~~~~~~~~~~~~~

- C++17 compatible compiler (GCC 9+, Clang 10+, MSVC 2019+)
- CMake 3.15+
- Git
- Python 3.8+ (for tests)

Clone and Build
~~~~~~~~~~~~~~~

.. code-block:: bash

   # Clone with submodules
   git clone --recursive https://github.com/teaguesterling/duckdb_webbed
   cd duckdb_webbed

   # Build release version
   make release

   # Run tests
   make test

Using devenv
~~~~~~~~~~~~

We use `devenv <https://devenv.sh>`_ for reproducible development environments:

.. code-block:: bash

   # Install devenv (see https://devenv.sh/getting-started/)
   # Then enter the development shell
   devenv shell

   # All dependencies are now available
   make release
   make test

Code Style
----------

- Follow the existing code style in the project
- Use clang-format for C++ code formatting
- Use clang-tidy for static analysis
- Pre-commit hooks will automatically format code on commit

Testing
-------

All changes must include tests. We use DuckDB's sqllogictest format:

.. code-block:: sql

   # name: test/sql/my_new_feature.test
   # description: Test description
   # group: [sql]

   require webbed

   # Test case description
   query I
   SELECT my_function('input');
   ----
   expected_output

Run specific tests:

.. code-block:: bash

   ./build/release/test/unittest "test/sql/my_new_feature.test"

Run all tests:

.. code-block:: bash

   make test

Pull Request Process
--------------------

1. **Fork the repository** and create a feature branch
2. **Write tests** for your changes
3. **Ensure all tests pass** (``make test``)
4. **Update documentation** if needed
5. **Submit a pull request** with a clear description

Commit Messages
~~~~~~~~~~~~~~~

Follow conventional commit format:

- ``feat: Add new function xyz``
- ``fix: Resolve issue with abc``
- ``docs: Update README``
- ``test: Add tests for xyz``

Areas for Contribution
----------------------

We welcome contributions in these areas:

**Performance**
   - Streaming support for large file sets (Issue #17)
   - Memory optimization for very large documents
   - Parallel file processing

**Features**
   - HTML form extraction
   - HTML metadata extraction (Open Graph, etc.)
   - XPath 2.0+ features
   - XSLT transformation support

**Documentation**
   - Tutorial content
   - More examples
   - Translation

**Testing**
   - Edge case coverage
   - Performance benchmarks
   - Cross-platform testing

Reporting Issues
----------------

When reporting issues:

1. Check existing issues first
2. Include DuckDB and extension version
3. Provide a minimal reproduction case
4. Include actual vs expected behavior

Code of Conduct
---------------

Be respectful and constructive in all interactions. We follow the `DuckDB Code of Conduct <https://duckdb.org/code_of_conduct>`_.

License
-------

By contributing, you agree that your contributions will be licensed under the MIT License.
