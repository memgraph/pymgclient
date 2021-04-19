=========
Changelog
=========

######
Future
######


****************
Breaking Changes
****************

******************************
Major Feature and Improvements
******************************

  * Include `mgclient` to decouple pymgclient from the installed version of
    `mgclient`, thus make the building and usage easier.

*********
Bug Fixes
*********

  * Fix various memory leaks.
  * Fix transaction handling when an error happens in explicit transactional
    mode. The running transaction is reset and a new one is going to be started
    with the next command.

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
