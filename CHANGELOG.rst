=========
Changelog
=========

######
1.5.0
######


******************************
Major Feature and Improvements
******************************

  * mgclient has been updated to 1.5.0
  * Update minimum Python requirement to 3.9
  * Support for zoned datetime objects with named IANA timezones or offsets
  * Remove deprecated `datetime.utcfromtimestamp()` usage in C extension


######
1.4.0
######


******************************
Major Feature and Improvements
******************************

  * Update CI to use newer Python versions
  * Update release workflow and CI infrastructure

######
1.3.1
######


******************************
Major Feature and Improvements
******************************

  * Use OpenSSL 1.1.1q and 3.0.5 versions for binary packages

*********
Bug Fixes
*********

  * Fixed import path of errors from `distutils`

######
1.3.0
######


******************************
Major Feature and Improvements
******************************

  * mgclient has been updated to 1.4.0
  * Support for OpenSSL 3
  * Use OpenSSL 1.1.1o and 3.0.3 versions for binary packages

######
1.2.1
######


******************************
Major Feature and Improvements
******************************

  * Use OpenSSL 1.1.1n for binary packages

######
1.2.0
######


******************************
Major Feature and Improvements
******************************

  * Add suport for arm64 macOS machines.
  * Link OpenSSL statically by default.
  * Add support for Python 3.10.

######
1.1.0
######


****************
Breaking Changes
****************

  * `pymgclient` is supported only for >3.7 python versions on Windows.

******************************
Major Feature and Improvements
******************************

  * Add support for temporal types.

*********
Bug Fixes
*********

######
1.0.0
######


****************
Breaking Changes
****************

******************************
Major Feature and Improvements
******************************

  * Include `mgclient` to decouple pymgclient from the installed version of
    `mgclient`, thus make the building and usage easier.
  * Add support for macOS and Windows.

*********
Bug Fixes
*********

  * Fix various memory leaks.
  * Fix transaction handling when an error happens in explicit transactional
    mode. The running transaction is reset and a new one is started with the
    next command.

######
0.1.0
######


****************
Breaking Changes
****************

******************************
Major Feature and Improvements
******************************

  * Initial implementation of DB-API 2.0 specification described by :pep:`249`.
