import pytest
import sys
import os
from pathlib import Path
from datetime import datetime
sys.path.append(os.path.dirname(os.getcwd()))
sys.path.append(Path(os.getcwd()).parent.absolute())
import lunaricorn
import lunaricorn.api.signaling as lsig
import lunaricorn.api.orb as orb
from lunaricorn.api.orb.client import OrbClient
from lunaricorn.types.orb_data_object import OrbDataObject
from lunaricorn.types.orb_meta_object import OrbMetaObject


@pytest.fixture(scope="session")
def orb_grpc_host():
    return "192.168.0.18"


@pytest.fixture(scope="session")
def orb_grpc_port():
    return 5558


@pytest.fixture
def orb_client(orb_grpc_host, orb_grpc_port):
    client = OrbClient(host=orb_grpc_host, port=orb_grpc_port)
    assert client.good(), "ORB gRPC server is not reachable"
    yield client
    client.close()

def test_client_good_returns_true(orb_client):
    assert orb_client.good() is True

def test_client_good_is_idempotent(orb_client):
    for _ in range(3):
        assert orb_client.good() is True

def test_create_orb_data(orb_client):
    data = {"hello": "world"}

    obj = orb_client.create_orb_data(data=data)

    assert obj is not None
    assert obj.u is not None
    assert obj.data == data

def test_push_orb_data(orb_client):
    obj = OrbDataObject(
        u=uuid.uuid7(),
        data={"x": 1},
        subtype="@json",
        type="@OrbData"
    )

    result = orb_client.push_orb_data(obj)
    assert result is obj


def test_fetch_orb_data_roundtrip(orb_client):
    created = orb_client.create_orb_data({"roundtrip": True})

    fetched = orb_client.fetch_orb_data(created.u)

    assert fetched is not None
    assert fetched.u == created.u
    assert fetched.data == created.data

def test_create_orb_meta(orb_client):
    meta = orb_client.create_orb_meta(handle=123)

    assert meta is not None
    assert meta.u is not None
    assert meta.handle == 123


def test_fetch_orb_meta_roundtrip(orb_client):
    created = orb_client.create_orb_meta(handle=456)

    fetched = orb_client.fetch_orb_meta(created.u)

    assert fetched is not None
    assert fetched.u == created.u
    assert fetched.handle == created.handle
