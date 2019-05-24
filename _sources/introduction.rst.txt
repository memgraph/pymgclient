============
Introduction
============

pymgclient is a `Memgraph <https://memgraph.com/>`_ database adapter for
Python language compliant with the DB-API 2.0 specification described by
:pep:`249`.

:py:mod:`mgclient` module is the current implementation of the adapter. It is
implemented in C as a wrapper around `mgclient`_, the official Memgraph client
library. As a C extension, it is only compatible with CPython implementation of
the Python programming language.

:py:mod:`mgclient` only works with Python 3.

#############
Prerequisites
#############

*******************
Build prerequisites
*******************

pymgclient is a C wrapper around the `mgclient`_ Memgraph client library. To
install it from sources you will need:

   - Python 3.5 or newer
   - A C compiler supporting C11 standard
   - Python header files
   - `mgclient`_ header files

To install from source, inside the source directory run::

   $ python3 setup.py build
   $ python3 setup.py install

********************
Runtime requirements
********************

:py:mod:`mgclient` module requires `mgclient` shared library at runtime
(usually distributed as :file:`libmgclient.so`). The module relies on the host
OS to find the location. If the library is installed in a standard location,
there should be no problems. Otherwise, you will have to let mgclient module
how to find it (usually by setting the :envvar:`LD_LIBRARY_PATH` environment
variable).

You will also need `OpenSSL <https://www.openssl.org/>`_ libraries required by
the `mgclient`_ C library.

######################
Running the test suite
######################

Once :py:mod:`mgclient` is installed, you can run the test suite to verify it
is working correctly. From the source directory, you can run::

   $ python3 -m pytest

To run the tests, you will need to have Memgraph, pytest and pyopenssl
installed on your machine. The tests will try to start the Memgraph binary from
the standard installation path (usually :file:`/usr/lib/memgraph/memgraph`)
listening on port 7687. You can configure a different path or port by setting
the following environment variables:

   * :envvar:`MEMGRAPH_PATH`
   * :envvar:`MEMGRAPH_PORT`

 .. _mgclient: https://github.com/memgraph/mgclient
