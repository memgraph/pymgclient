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

Check out the documentation if you need help with
[installation](https://memgraph.github.io/pymgclient/introduction.html#installation)
or if you want to
[build](https://memgraph.github.io/pymgclient/introduction.html#install-from-source)
pymgclient for yourself!
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
