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

import os
import tempfile

import mgclient
import pytest

from common import start_memgraph, Memgraph, requires_ssl_enabled, requires_ssl_disabled
from OpenSSL import crypto


@pytest.fixture(scope="function")
def memgraph_server():
    memgraph = start_memgraph()
    yield memgraph.host, memgraph.port, memgraph.sslmode(), memgraph.is_long_running()

    memgraph.terminate()


def generate_key_and_cert(key_file, cert_file):
    k = crypto.PKey()
    k.generate_key(crypto.TYPE_RSA, 4096)

    cert = crypto.X509()
    cert.get_subject().C = "CA"
    cert.get_subject().O = "server"
    cert.get_subject().CN = "localhost"
    cert.set_serial_number(1)
    cert.gmtime_adj_notBefore(0)
    cert.gmtime_adj_notAfter(86400)
    cert.set_issuer(cert.get_subject())
    cert.set_pubkey(k)
    cert.sign(k, "sha512")

    cert_file.write(crypto.dump_certificate(crypto.FILETYPE_PEM, cert))
    cert_file.flush()
    key_file.write(crypto.dump_privatekey(crypto.FILETYPE_PEM, k))
    key_file.flush()


@pytest.fixture(scope="function")
def secure_memgraph_server():
    # we need public/private key pair to run Memgraph with SSL
    with tempfile.NamedTemporaryFile() as key_file, tempfile.NamedTemporaryFile() as cert_file:
        generate_key_and_cert(key_file.file, cert_file.file)

        memgraph = start_memgraph(key_file=key_file.name, cert_file=cert_file.name)
        assert memgraph.use_ssl
        assert memgraph.sslmode() == mgclient.MG_SSLMODE_REQUIRE
        yield memgraph.host, memgraph.port, memgraph.is_long_running()

    memgraph.terminate()


@pytest.fixture(scope="function")
def provide_role():
    memgraph = start_memgraph()
    conn = mgclient.connect(
        host=memgraph.host,
        port=memgraph.port,
    )
    conn.autocommit = True
    cursor = conn.cursor()
    cursor.execute("CREATE ROLE architect;")
    memgraph.terminate()

    yield None

    memgraph = start_memgraph()
    conn = mgclient.connect(
        host=memgraph.host,
        port=memgraph.port,
    )
    conn.autocommit = True
    cursor = conn.cursor()
    cursor.execute("DROP ROLE architect;")
    memgraph.terminate()


@pytest.fixture(scope="function")
def auth_module_path():
    yield os.path.normpath(
        os.path.join(
            os.path.dirname(os.path.realpath(__file__)),
            "auth_module",
            "dummy_auth_module.py",
        )
    )


def test_connect_args_validation():
    # bad port
    with pytest.raises(ValueError):
        mgclient.connect(host="127.0.0.1", port=12344567)

    # bad SSL mode
    with pytest.raises(ValueError):
        mgclient.connect(host="127.0.0.1", port=7687, sslmode=55)

    # trust_callback not callable
    with pytest.raises(TypeError):
        mgclient.connect(
            host="127.0.0.1",
            port=7687,
            sslmode=mgclient.MG_SSLMODE_REQUIRE,
            trust_callback="not callable",
        )


def test_connect_with_custom_auth_scheme(provide_role, auth_module_path):
    custom_scheme = "custom_scheme"
    memgraph = start_memgraph(
        auth_module_mappings=f"{custom_scheme}:{auth_module_path}"
    )
    conn = mgclient.connect(
        host=memgraph.host,
        port=memgraph.port,
        scheme=custom_scheme,
        username="andy",
        password="dummy auth token",
    )
    assert conn.status == mgclient.CONN_STATUS_READY
    memgraph.terminate()


@requires_ssl_disabled
def test_connect_insecure_success(memgraph_server):
    host, port, sslmode, _ = memgraph_server
    assert sslmode == mgclient.MG_SSLMODE_DISABLE
    conn = mgclient.connect(host=host, port=port, sslmode=sslmode)

    assert conn.status == mgclient.CONN_STATUS_READY


@requires_ssl_disabled
def test_connection_secure_fail(memgraph_server):
    # server doesn't use SSL
    host, port, sslmode, _ = memgraph_server
    with pytest.raises(mgclient.OperationalError):
        mgclient.connect(host=host, port=port, sslmode=mgclient.MG_SSLMODE_REQUIRE)


@requires_ssl_enabled
def test_connection_secure_success(secure_memgraph_server):
    host, port, is_long_running = secure_memgraph_server

    with pytest.raises(mgclient.OperationalError):
        conn = mgclient.connect(host=host, port=port)

    def good_trust_callback(hostname, ip_address, key_type, fingerprint):
        if not is_long_running:
            assert hostname == "localhost"
            assert ip_address == "127.0.0.1"
        return True

    def bad_trust_callback(hostname, ip_address, key_type, fingerprint):
        if not is_long_running:
            assert hostname == "localhost"
            assert ip_address == "127.0.0.1"
        return False

    with pytest.raises(mgclient.OperationalError):
        conn = mgclient.connect(
            host=host,
            port=port,
            sslmode=mgclient.MG_SSLMODE_REQUIRE,
            trust_callback=bad_trust_callback,
        )

    conn = mgclient.connect(
        host=host,
        port=port,
        sslmode=mgclient.MG_SSLMODE_REQUIRE,
        trust_callback=good_trust_callback,
    )

    assert conn.status == mgclient.CONN_STATUS_READY


def test_connection_close(memgraph_server):
    host, port, sslmode, _ = memgraph_server
    conn = mgclient.connect(host=host, port=port, sslmode=sslmode)

    assert conn.status == mgclient.CONN_STATUS_READY

    conn.close()
    assert conn.status == mgclient.CONN_STATUS_CLOSED

    # closing twice doesn't do anything
    conn.close()
    assert conn.status == mgclient.CONN_STATUS_CLOSED

    with pytest.raises(mgclient.InterfaceError):
        conn.commit()

    with pytest.raises(mgclient.InterfaceError):
        conn.rollback()

    with pytest.raises(mgclient.InterfaceError):
        conn.cursor()


def test_connection_close_lazy(memgraph_server):
    host, port, sslmode, _ = memgraph_server
    conn = mgclient.connect(host=host, port=port, lazy=True, sslmode=sslmode)
    cursor = conn.cursor()

    assert conn.status == mgclient.CONN_STATUS_READY

    cursor.execute("RETURN 100")
    assert conn.status == mgclient.CONN_STATUS_EXECUTING

    with pytest.raises(mgclient.InterfaceError):
        conn.close()

    cursor.fetchall()

    conn.close()
    assert conn.status == mgclient.CONN_STATUS_CLOSED


def test_autocommit_regular(memgraph_server):
    host, port, sslmode, _ = memgraph_server

    conn = mgclient.connect(host=host, port=port, sslmode=sslmode)

    # autocommit should be turned off by default
    assert not conn.autocommit

    cursor = conn.cursor()
    cursor.execute("RETURN 5")
    assert conn.status == mgclient.CONN_STATUS_IN_TRANSACTION

    # can't update autocommit while in transaction
    with pytest.raises(mgclient.InterfaceError):
        conn.autocommit = True

    conn.rollback()
    conn.autocommit = True

    assert conn.autocommit

    assert conn.status == mgclient.CONN_STATUS_READY
    cursor.execute("RETURN 5")
    assert conn.status == mgclient.CONN_STATUS_READY

    with pytest.raises(mgclient.InterfaceError):
        del conn.autocommit


def test_autocommit_lazy(memgraph_server):
    host, port, sslmode, _ = memgraph_server

    conn = mgclient.connect(host=host, port=port, lazy=True, sslmode=sslmode)

    # autocommit is always true for lazy connections
    assert conn.autocommit

    with pytest.raises(mgclient.InterfaceError):
        conn.autocommit = False


def test_commit(memgraph_server):
    host, port, sslmode, is_long_running = memgraph_server

    conn1 = mgclient.connect(host=host, port=port, sslmode=sslmode)
    conn2 = mgclient.connect(host=host, port=port, sslmode=sslmode)
    conn2.autocommit = True

    cursor1 = conn1.cursor()
    cursor1.execute("MATCH (n) RETURN count(n)")
    original_count = cursor1.fetchall()[0][0]
    assert is_long_running or original_count == 0

    cursor1.execute("CREATE (:Node)")

    cursor2 = conn2.cursor()
    cursor2.execute("MATCH (n) RETURN count(n)")
    assert cursor2.fetchall() == [(original_count,)]

    conn1.commit()

    cursor2.execute("MATCH (n) RETURN count(n)")
    assert cursor2.fetchall() == [(original_count + 1,)]


def test_rollback(memgraph_server):
    host, port, sslmode, is_long_running = memgraph_server

    conn = mgclient.connect(host=host, port=port, sslmode=sslmode)

    cursor = conn.cursor()

    cursor.execute("MATCH (n) RETURN count(n)")
    original_count = cursor.fetchall()[0][0]
    assert is_long_running or original_count == 0

    cursor.execute("CREATE (:Node)")
    cursor.fetchall()

    cursor.execute("MATCH (n) RETURN count(n)")
    assert cursor.fetchall() == [(original_count + 1,)]

    conn.rollback()
    cursor.execute("MATCH (n) RETURN count(n)")
    assert cursor.fetchall() == [(original_count,)]


def test_close_doesnt_commit(memgraph_server):
    host, port, sslmode, is_long_running = memgraph_server

    conn = mgclient.connect(host=host, port=port, sslmode=sslmode)

    cursor = conn.cursor()
    cursor.execute("MATCH (n) RETURN count(n)")
    original_count = cursor.fetchall()[0][0]
    assert is_long_running or original_count == 0

    cursor.execute("CREATE (:Node)")

    conn.close()

    conn = mgclient.connect(host=host, port=port, sslmode=sslmode)
    cursor = conn.cursor()
    cursor.execute("MATCH (n) RETURN count(n)")

    assert cursor.fetchall() == [(original_count,)]


def test_commit_rollback_lazy(memgraph_server):
    host, port, sslmode, _ = memgraph_server
    conn = mgclient.connect(host=host, port=port, lazy=True, sslmode=sslmode)
    cursor = conn.cursor()
    cursor.execute("CREATE (:Node) RETURN 1")

    conn.rollback()
    assert conn.status == mgclient.CONN_STATUS_EXECUTING

    conn.commit()
    assert conn.status == mgclient.CONN_STATUS_EXECUTING

    assert cursor.fetchall() == [(1,)]
    assert conn.status == mgclient.CONN_STATUS_READY


def test_autocommit_failure(memgraph_server):
    host, port, sslmode, _ = memgraph_server
    conn = mgclient.connect(host=host, port=port, sslmode=sslmode)
    conn.autocommit = False

    assert conn.status == mgclient.CONN_STATUS_READY
    cursor = conn.cursor()
    cursor.execute("RETURN 5")
    assert conn.status == mgclient.CONN_STATUS_IN_TRANSACTION

    with pytest.raises(mgclient.DatabaseError):
        cursor.execute("SHOW INDEX INFO")

    assert conn.status == mgclient.CONN_STATUS_READY
    cursor.execute("RETURN 5")
    assert conn.status == mgclient.CONN_STATUS_IN_TRANSACTION
