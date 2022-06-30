============
Introduction
============

pymgclient is a `Memgraph <https://memgraph.com/>`_ database adapter for Python
language compliant with the DB-API 2.0 specification described by :pep:`249`.

:py:mod:`mgclient` module is the current implementation of the adapter. It is
implemented in C as a wrapper around `mgclient`_, the official Memgraph client
library. As a C extension, it is only compatible with CPython implementation of
the Python programming language.

:py:mod:`mgclient` only works with Python 3.


#############
Installation
#############

pymgclient has prebuilt binary packages for

* macOS Catalina (version 10.15) and newer on x86_64 with `Python
  <https://www.python.org/downloads/>`_ 3.7+

* macOS Monterey (version 12) and newer on arm64 with `Python
  <https://www.python.org/downloads/>`_ 3.8+

* Windows 10 x86_64 with `Python <https://www.python.org/downloads/>`_ 3.7+

To intall pymgclient binaries on these platforms see `Install binaries`_ section
or check `Install from source`_ for other platforms.

Install binaries
################

.. warning::
    All of the binary packages are statically linked against OpenSSL, that means the
    version of OpenSSL they are using is fixed. If security is important for you,
    you should check how to build pymgclient with dynamically linked OpenSSL, so
    pymgclient can use the latest version of OpenSSL that is installed on your
    machine.

On macOS run::

  $ pip3 install --user pymgclient

On Windows run::

  $ py -3 -m pip install --user pymgclient

Alternatively, on Windows, if the launcher is not installed, just run::

  $ pip install --user pymgclient

Install from source
###################

pymgclient can be installed from source on:

* all platforms that have prebuilt binaries
* on various Linux distributions, including:

  * Ubuntu 18.04+
  * Debian 10+
  * CentOS 8+

*******************
Build prerequisites
*******************

pymgclient is a C wrapper around the `mgclient`_ Memgraph client library. To
build it from you will need:

* Python 3.6 or newer
* Python 3.6 or newer header files
* A C compiler supporting C11 standard
* A C++ compiler (it is not used directly, but necessary for CMake to work)
* Preqrequisites of `mgclient`_:

  * CMake 3.8 or newer
  * OpenSSL 1.0.2 or newer (including OpenSSL 3.0.0+) and its header files

Building on Linux
*****************

First install the prerequisites:

* On Debian/Ubuntu::

  $ sudo apt install python3-dev cmake make gcc g++ libssl-dev
* On CentOS::

  $ sudo yum install -y python3-devel cmake3 make gcc gcc-c++ openssl-devel

After the prerequisites are installed pymgclient can be installed via pip::

  $ pip3 install --user pymgclient

If you want to dynamically link OpenSSL for better security, you can use the
following command::

  $ pip3 install --user \
                 --global-option=build_ext \
                 --global-option="--static-openssl=false" \
                 pymgclient

This will download the source package of pymgclient and build the binary package
before installing it. Alternatively, pymgclient can be installed by using
:file:`setup.py`::

  $ python3 setup.py install

Building on macOS
*****************

To install the C/C++ compiler, run::

  $ xcode-select --install

The rest of the build prerequisites can be installed easily via `brew`_::

  $ brew install python3 openssl cmake

It is important to mention that on M1/ARM machines pymgclient cannot be built
with the default installed Python version, thus Python needs to be installed via
brew. If you are interested in the technical details, you can find more details
in the technical notes below.

After the prerequisites are installed pymgclient can be installed via pip::

  $ pip3 install --user pymgclient --no-binary :all:

This will download the source package of pymgclient and build the binary package
before installing it. If you want to dynamically link OpenSSL for better
security, you can use the following command::

  $ pip3 install --user \
                 --global-option=build_ext \
                 --global-option="--static-openssl=false" \
                 pymgclient \
                 --no-binary :all:

Alternatively, pymgclient can be installed by using :file:`setup.py`::

  $ python3 setup.py install

Technical note for arm64 machines
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The default installed Python is in the so called `Universal Binary 2
<https://en.wikipedia.org/wiki/Universal_binary#Universal_2>`_ format. That
means all of the packages that are built with this Python version have to be
built also as a universal binary. Though pymgclient builds on both x86_64 and
arm64 architectures, the brew installed OpenSSL version only contains the arm64
binaries. As a consequence, during building the x86_64 part of the universal
binary of pymgclient, the linker fails, because it cannot find the OpenSSL
binaries in x86_64 binary format.

Building on Windows
*******************

Building pymgclient on Windows is only advised for advanced users, therefore the
following description assumes technical knowledge about Windows, compiling C/C++
applications and Python package.

To build pymgclient on Windows, the `MSYS2 <https://www.msys2.org/>`_
environment is needed. Once it is installed, run "MSYS2 MSYS" from Start menu
and install the necessary packages::

  $ pacman -Su
  $ pacman -S --needed base-devel mingw-w64-x86_64-toolchain \
      mingw-w64-x86_64-cmake mingw-w64-x86_64-openssl

After installation, add the :file:`<path to msys>/mingw64/bin` (by default this
is :file:`C:/msys64/mingw64/bin`) to the :envvar:`PATH` environment variable to
make the installed applications accessible from the default Windows command
prompt. Once it is done, start the Windows command prompt and make sure the
applications are available, e.g. checking the version of gcc::

  $ gcc --version

When the environment is done, start the Windows command prompt and install
pymgclient can be installed via pip::

  $ pip install --user pymgclient --no-binary :all:

If you want to dynamically link OpenSSL for better security, you can use the
following command::

  $ pip install --user \
                --global-option=build_ext \
                --global-option="--static-openssl=false" \
                pymgclient \
                --no-binary :all:

Alternatively, pymgclient can be installed by using :file:`setup.py`::

  $ python setup.py install

######################
Running the test suite
######################

If pymgclient is installed from downloaded source, you can run the test suite to
verify it is working correctly. From the source directory, you can run::

  $ python3 -m pytest

To run the tests, you will need to have Memgraph, pytest and pyopenssl installed
on your machine. The tests will try to start the Memgraph binary from the
standard installation path (usually :file:`/usr/lib/memgraph/memgraph`)
listening on port 7687. You can configure a different path or port by setting
the following environment variables:

* :envvar:`MEMGRAPH_PATH`
* :envvar:`MEMGRAPH_PORT`

Alternatively you can also run the tests with an already running Memgraph by
configuring the host and port by setting the following environment variables:

* :envvar:`MEMGRAPH_HOST`
* :envvar:`MEMGRAPH_PORT`

When an already running Memgraph is used, then some of the tests might get
skipped if Memgraph hasn't been started with a suitable configuration. The
:envvar:`MEMGRAPH_STARTED_WITH_SSL` environment variable can be used to indicate
whether Memgraph is started using SSL or not. If the environment variable is
defined (regardless of its value), then the tests connect via secure Bolt
connection, otherwise they connect with regular Bolt connection.

The **tests insert data into Memgraph**, so they shouldn't be used with a
Memgraph running in "production" environment.

 .. _mgclient: https://github.com/memgraph/mgclient
 .. _brew: https://brew.sh
