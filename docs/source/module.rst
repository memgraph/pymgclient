===============================
:mod:`mgclient` module content
===============================

The module interface respects the DB-API 2.0 standard defined in :pep:`249`.

.. py:module:: mgclient

.. autofunction:: mgclient.connect

See :ref:`lazy-connections` section to learn about
advantages and limitations of using the ``lazy`` parameter.

#####################
Client-side routing
#####################

When connecting to a Memgraph high-availability cluster, passing
``routing=True`` to :func:`connect` makes the connection routing-aware: the
given ``host``/``port`` must point at a cluster coordinator, from which the
current cluster topology is fetched (via a Bolt ``ROUTE`` message) and a data
instance matching the requested ``access_mode`` is selected and connected to.

.. data:: mgclient.ACCESS_MODE_WRITE

   ``access_mode`` value selecting a server that accepts writes (the cluster
   main). This is the default.

.. data:: mgclient.ACCESS_MODE_READ

   ``access_mode`` value selecting a server that serves reads (a replica).

If the addresses a cluster advertises are not directly reachable by the client
(for example when the cluster is reached through a proxy or a port-forward),
pass a ``resolver`` callable that maps an advertised ``"host:port"`` address to
an iterable of ``"host:port"`` targets to try.

``connect(routing=True, ...)`` performs a fresh routing lookup on every call.
For a long-lived router that caches the routing table (honouring its TTL),
balances reads across replicas and fails over across coordinators, use the
:class:`Router` class:

.. autoclass:: mgclient.Router
   :members: connect, execute_read, execute_write, refresh, routing_table

:meth:`Router.execute_read` and :meth:`Router.execute_write` are *managed
transactions*: they run your unit of work against the right instance and
automatically retry transient failover conditions (a new main being elected, a
replica catching up, an instance dropping mid-request) with a routing refresh
and capped exponential backoff. A write that committed on the main but could
not reach a synchronous replica is treated as success rather than retried
(retrying would duplicate it).

The classification used for retries is also exposed for building your own retry
loops:

.. autofunction:: mgclient.is_transient_error

.. autofunction:: mgclient.is_committed_on_main_error

For lower-level access to the routing table itself, see
:meth:`Connection.get_routing_table`.

################
Module constants
################

DB-API 2.0 requires the following constants to be defined:

.. data:: mgclient.apilevel

   String constant stating the supported DB API level. For :mod:`mgclient` it
   is ``2.0``.

.. data:: mgclient.threadsafety

   Integer constant stating the level of thread safety the interface supports.
   For :mod:`mgclient` it is 1, meaning that threads may share the module, but
   not connections.

.. data:: mgclient.paramstyle

   String constant stating the type of parameter marker formatting expected by
   the interface. For :mod:`mgclient` it is ``cypher``, which is not a valid
   value by DB-API 2.0 specification. See Passing parameters section for more
   details.

##########
Exceptions
##########

By DB-API 2.0 specification, the module makes all error information available
through these exceptions or subclasses thereof:

.. autoexception:: mgclient.Warning

.. autoexception:: mgclient.Error

.. autoexception:: mgclient.InterfaceError

.. autoexception:: mgclient.DatabaseError

.. autoexception:: mgclient.DataError

.. autoexception:: mgclient.OperationalError

.. autoexception:: mgclient.IntegrityError

.. autoexception:: mgclient.InternalError

.. autoexception:: mgclient.ProgrammingError

.. autoexception:: mgclient.NotSupportedError

.. NOTE::

   In the current state, :exc:`OperationalError` is raised for all errors
   obtained from the database. This will probably be improved in the future.

##################
Graph type objects
##################

.. autoclass:: mgclient.Node

   .. autoattribute:: mgclient.Node.id
   .. autoattribute:: mgclient.Node.labels
   .. autoattribute:: mgclient.Node.properties

.. autoclass:: mgclient.Relationship

   .. autoattribute:: mgclient.Relationship.id
   .. autoattribute:: mgclient.Relationship.start_id
   .. autoattribute:: mgclient.Relationship.end_id
   .. autoattribute:: mgclient.Relationship.type
   .. autoattribute:: mgclient.Relationship.properties


.. autoclass:: mgclient.Path
   :members:
