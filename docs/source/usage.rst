==================
Basic module usage
==================

.. py:module:: mgclient

Basic :mod:`mgclient` module usage is similar to that of all the database
adapters compliant the DB-API 2.0. To use the module, you must:

   1. Create a :class:`Connection` object, using the :func:`.connect`
      function.

   2. Create a :class:`Cursor` object by calling :meth:`.cursor` on
      the :class:`Connection` object.

   3. Call cursor's :meth:`.execute` method to perform openCypher queries.

   4. Make database changes persistent by calling :meth:`.commit`, or drop them
      by calling :meth:`.rollback`.

Here is an example of an interactive session
showing some of the basic commands::

   >>> import mgclient

   # Make a connection to the database
   >>> conn = mgclient.connect(host='127.0.0.1', port=7687)

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

########################################
Passing parameters to openCypher queries
########################################

Usually, your openCypher queries will need to use the values from Python
variables. You shouldn't assemble your query using Python's string operators
because doing so is insecure.

Instead, you should use the parameter substitution mechanism built into
Memgraph. Put ``$name`` as a placeholder wherever you want to use a value, and
the provide a dictionary mapping names to values as the second argument to the
cursor's :meth:`.execute`. For example::

   # Don't do this!
   server_id = 'srvr-38219012-sw'
   c.execute("MATCH (s:Server {id: '%s'}) SET s.hits = s.hits + 1"
             % server_id)

   # Instead, do this
   c.execute("MATCH (s:Server {id: $sid}) SET s.hits = s.htis + 1",
             {'sid': server_id})


###############################################
Adaptation of Memgraph values to Python types
###############################################

The following table shows the mapping between Python and Memgraph types:

=============   ===============================
Memgraph        Python
=============   ===============================
Null            :const:`None`
Boolean         :class:`bool`
Integer         :class:`int`
Float           :class:`float`
String          :const:`str`
Date            :class:`datetime.date`
LocalTime       :class:`datetime.time`
LocalDateTime   :class:`datetime.datetime`
Duration        :class:`datetime.timedelta`
List            :class:`list`
Map             :class:`dict`
Node            :class:`mgclient.Node`
Relationship    :class:`mgclient.Relationship`
Path            :class:`mgclient.Path`
=============   ===============================

Note that in Bolt protocol, all string data is represented as UTF-8 encoded
binary data.

####################
Transactions control
####################

In :mod:`mgclient` transactions are handled by the :class:`Connection` class.
By default, the first time a command is sent to the database using one of the
:class:`Cursor` objects created by it, a new transaction is started (by sending
``BEGIN`` command to Memgraph). All following commands (issued by any of the
cursors) will be executed in the context of the same transaction. If any of the
commands fails, the transaction will not be able to commit and no further
command will successfuly execute until :meth:`.rollback` is called.

The connection is responsible for terming its transaction, either by calling
:meth:`.commit` or :meth:`.rollback`. Closing the connection using
:meth:`Connection.close` or destroying the connection object results
in an implicit rollback.

You can set the connection in *autocommit* mode: that way all commands executed
will be immediately committed and no rollback is possible. A few commands
(``CREATE INDEX``, ``CREATE USER`` and similar) require to be run outside any
transaction. To set the connection in *autocommit* mode, set
:attr:`.autocommit` property of the connection to ``True``.
