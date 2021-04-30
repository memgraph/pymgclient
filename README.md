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

  - [Python](https://www.python.org/) 3.6 or newer
  - [Python](https://www.python.org/) 3.6 or newer header files
  - A C compiler supporting C11 standard
  - Preqrequisites of [mgclient](mgclient):
    - [CMake](https://cmake.org/) 3.8 or newer
    - [OpenSSL](https://www.openssl.org/) 1.0.2 or newer

Though [mgclient](mgclient) mentions Apple LLVM/clang as a build requirement,
it doesn't hold for pymgclient, because it is only necessary for building the
tests for mgclient. As pymgclient has its own tests, the tests of mgclient are
not built when building pymgclient.

By default pymgclient will try to use `cmake3` and `cmake` (in this order) to
call CMake, if the `PYMGCLIENT_CMAKE` environment variable is not set.
Otherwise the value of `PYMGCLIENT_CMAKE` will be used without further checks.

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

#### Building on Windows

If the binary packages from PyPI don't work for you, then you can build your
own version of pymgclient. Currently only 64bit versions are built and tested.

In order to build pymgclient on Windows, you have setup the necessary
[environment](https://github.com/memgraph/mgclient#building-and-installing-on-windows)
to build mgclient. Once it is done, add the `<path to msys>\mingw64\bin`
folder to the `PATH` environment variable. After that the  python commands
shown above should work from the Windows command prompt.

### Runtime requirements

You will need [OpenSSL](https://www.openssl.org/) libraries required by
the [mgclient](mgclient) C library.

## Running the test suite

Once pymgclient is installed, you can run the test suite to verify it is
working correctly. From the source directory, you can run:

```
$ python3 -m pytest
```

To run the tests, you will need to have Memgraph, pytest and pyopenssl
installed on your machine. The tests will try to start the Memgraph binary from
the standard installation path (usually `/usr/lib/memgraph/memgraph`) listening
on port 7687. You can configure a different path or port by setting the
following environment variables:

  - `MEMGRAPH_PATH`
  - `MEMGRAPH_PORT`

Alternatively you can also run the tests with an already running Memgraph
by configuring the host and port by setting the following environment
variables:

  - `MEMGRAPH_HOST`
  - `MEMGRAPH_PORT`

When an already running Memgraph is used, then some of the tests might get
skipped if Memgraph hasn't been started with a suitable configuration. The
`MEMGRAPH_STARTED_WITH_SSL` environment variable can be used to indicate
whether Memgraph is started using SSL or not. If the environment variable is
defined (regardless its value), then the tests connect via secure Bolt
connection, otherwise they connect with regular Bolt connection.

The **tests insert data into Memgraph**, so they shouldn't be used with
a Memgraph running in "production" environment.
## Documentation

Online documentation can be found on [GitHub
pages](https://memgraph.github.io/pymgclient/).

You can also build a local version of the documentation by running `make` from
the `docs` directory. You will need [Sphinx](http://www.sphinx-doc.org/)
installed in order to do that.

## Code sample

Here is an example of an interactive session showing some of the basic
commands:

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
