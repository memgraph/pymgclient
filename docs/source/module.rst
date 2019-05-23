===============================
:mod:`mgclient` module content
===============================

The module interface respects the DB-API 2.0 standard defined in :pep:`249`.

.. py:module:: mgclient

.. autofunction:: mgclient.connect

See :ref:`lazy-connections` section to learn about
advantages and limitations of using the ``lazy`` parameter.

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
