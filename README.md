[![Actions Status](https://github.com/memgraph/pymgclient/workflows/CI/badge.svg)](https://github.com/memgraph/pymgclient/actions)

# pymgclient - Memgraph database adapter for Python language

pymgclient is a [Memgraph](https://memgraph.com) database adapter for Python
programming language compliant with the DB-API 2.0 specification described by
PEP 249.

mgclient module is the current implementation of the adapter. It is implemented
in C as a wrapper around [mgclient](https://github.com/memgraph/mgclient), the
official Memgraph client library. As a C extension, it is only compatible with
the CPython implementation of the Python programming language.

pymgclient only works with Python 3.

## Prerequisites

### Build prerequisites

pymgclient is a C wrapper around the
[mgclient](https://github.com/memgraph/mgclient) Memgraph client library. To
install it from sources you will need:

  * Python 3.5 or newer
  * A C compiler supporting C11 standard
  * Python header files

Once prerequisites are met, you can install pymgclient using `pip` to download
it from PyPI:

```
$ pip3 install pymgclient
```

or using `setup.py` if you have downloaded the source package locally:

```
$ python3 setup.py build
$ python3 setup.py install
```

### Runtime requirements

You will need [OpenSSL](https://www.openssl.org/) libraries required by
the [mgclient](https://github.com/memgraph/mgclient) C library.

## Running the test suite

Once mgclient is installed, you can run the test suite to verify it is working
correctly. From the source directory, you can run:

```
$ python3 -m pytest
```

To run the tests, you will need to have Memgraph, pytest and pyopenssl
installed on your machine. The tests will try to start the Memgraph binary from
the standard installation path (usually `/usr/lib/memgraph/memgraph`) listening
on port 7687. You can configure a different path or port by setting the
following environment variables:

  * `MEMGRAPH_PATH`
  * `MEMGRAPH_PORT`

## Documentation

Online documentation can be found on [GitHub
pages](https://memgraph.github.io/pymgclient/).

You can also build a local version of the documentation by running `make` from
the `docs` directory. You will need [Sphinx](http://www.sphinx-doc.org/)
installed in order to do that.

## Code sample

Here is an example of an interactive session showing some of the basic commands:

```python
>>> import mgclient

# Make a connection to the database
>>> conn = mgclient.connect(host='127.0.0.1', port=7687, sslmode=mgclient.MG_SSLMODE_REQUIRE)

# Create a cursor for query execution
>>> cursor = conn.cursor()

# Execute a query
>>> cursor.execute("""
        CREATE (n:Person {name: 'John'})-[e:KNOWS]->
               (m:Person {name: 'Steve'})
        RETURN n, e, m
    """)

# Fetch one row of query results
>>> row = cursor.fetchone()

>>> print(row[0])
(:Person {'name': 'John'})

>>> print(row[1])
[:KNOWS]

>>> print(row[2])
(:Person {'name': 'Steve'})

# Make database changes persistent
>>> conn.commit()
```
