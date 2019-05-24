===============================
:class:`Connection` class
===============================

.. py:module:: mgclient

.. autoclass:: mgclient.Connection
   :members:

.. _lazy-connections:

################
Lazy connections
################

When a query is executed using :meth:`.execute` on a cursor, the default
:mod:`mgclient` behaviour is to wait for all of the results to be available
and store them into cursor's internal result list. On one hand, that means that
:meth:`.execute` will block until all of the results are ready and all results
will be stored in memory at the same time. On the other hand, that also means
that result fetching methods will never block.

Sometimes, that behaviour is unwanted. Maybe we don't need all results in
memory at the same time, because we only want to do row-by-row processing on
huge result sets. In that case, we may use a lazy connection.

A lazy connection is created by passing ``True`` for ``lazy`` parameter when
calling :func:`.connect`. Cursors created using lazy connections will only try
to read results from the network socket when :meth:`.fetchone`,
:meth:`.fetchmany` or :meth:`.fetchall` is called. Also, they can allocate less
memory because they don't have to store the entire result set in memory at
once.

However, lazy connections have two limitations:

   1. They are always in autocommit mode. If necessary, transactions can be
      explicitly managed by executing ``BEGIN``, ``COMMIT`` or ``ROLLBACK``
      queries.

   2. At most one query can execute at a given time. Trying to execute multiple
      queries at once will raise an :exc:`InterfaceError`.

      Before trying to execute a new query, all results of the previous query
      must be fetched from its corresponding cursor (for example by calling
      :meth:`.fetchone` until it returns :obj:`None`, or by calling
      :meth:`.fetchmany`).

Here's an example usage of a lazy connection::

   >>> import mgclient

   >>> conn = mgclient.connect(host="127.0.0.1", port=7687, lazy=True)

   >>> cursor = conn.cursor()

   >>> cursor.execute("UNWIND range(1, 3) AS n RETURN n * n")

   >>> cursor.fetchone()
   (1, )

   >>> cursor.fetchone()
   (4, )

   >>> cursor.fetchone()
   (9, )

   # We still didn't get None from fetchone()
   >>> cursor.execute("RETURN 100")
   Traceback (most recent call last):
   File "<stdin>", line 1, in <module>
   mgclient.InterfaceError: cannot call execute during execution of a query

   >>> cursor.fetchone()
   None

   # Now we can execute a new query
   >>> cursor.execute("RETURN 100")

