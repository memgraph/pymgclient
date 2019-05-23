# pymgclient -- Memgraph database adapter for Python language

pymgclient is a [Memgraph](https://memgraph.com/>) database adapter for
Python language compliant with the DB-API 2.0 specification described by
PEP 249.

`mgclient` module is the current implementation of the adapter. It is
implemented in C as a wrapper around
[mgclient](https://github.com/memgraph/mgclient), the official Memgraph client
library. As a C extension, it is only compatible with CPython implementation of
the Python programming language.

`mgclient` only works with Python 3.
