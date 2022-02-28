=========
Changelog
=========

######
1.1.1
######


******************************
Major Feature and Improvements
******************************

  * Add suport for arm64 macOS machines.

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
