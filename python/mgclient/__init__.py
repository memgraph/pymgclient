# Copyright (c) 2016-2020 Memgraph Ltd. [https://memgraph.com]
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Memgraph database adapter for Python, compliant with the DB-API 2.0
specification described by :pep:`249`.

The compiled functionality lives in the :mod:`mgclient._mgclient` C extension
and is re-exported here unchanged, so ``import mgclient`` behaves exactly as it
always has.  On top of that this package adds optional client-side routing for
Memgraph high-availability clusters (see :func:`connect` and the routing
constants below).
"""

# Re-export everything the C extension provides (connect, Connection, Cursor,
# the exception hierarchy, the type objects and all module constants) so that
# the public ``mgclient`` API is unchanged.
from mgclient._mgclient import *  # noqa: F401,F403

# The routing-aware ``connect`` wrapper replaces the C ``connect`` imported
# above; with routing disabled (the default) it delegates straight to it, so
# existing code is unaffected.
from mgclient.routing import (  # noqa: F401,E402
    ACCESS_MODE_READ,
    ACCESS_MODE_WRITE,
    Router,
    connect,
    is_committed_on_main_error,
    is_transient_error,
)
