import grpc
from grpc import ChannelConnectivity
from typing import Optional, List, Union
import uuid
import json
from datetime import datetime, timezone
from google.protobuf.json_format import MessageToDict, ParseDict

from lunaricorn.api.orb.datastorage_pb2 import (
    OrbDataObject as ProtoOrbDataObject,
    OrbMetaObject as ProtoOrbMetaObject,
    OrbDataRequest,
    OrbMetaRequest,
    FetchOrbRequest,
    PushDataRequest,
    PushMetaRequest,
    FetchRequest
)
from lunaricorn.api.orb.datastorage_pb2_grpc import OrbDataServiceStub
from lunaricorn.types.orb_data_object import OrbDataObject, OrbDataSybtypes
from lunaricorn.types.orb_meta_object import OrbMetaObject


class OrbClient:
    def __init__(self, host: str = 'localhost', port: int = 50051):
        self.channel = grpc.insecure_channel(f'{host}:{port}')
        self.stub = OrbDataServiceStub(self.channel)
        
    def good(self, timeout: float = 2.0) -> bool:
        try:
            state = self.channel.get_state(try_to_connect=True)
            if state == ChannelConnectivity.READY:
                return True
            grpc.channel_ready_future(self.channel).result(timeout=timeout)
            return True

        except grpc.FutureTimeoutError:
            return False
        except Exception:
            return False

    def close(self):
        self.channel.close()
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    @staticmethod
    def _orb_data_to_proto(orb_data: OrbDataObject) -> ProtoOrbDataObject:
        proto_obj = ProtoOrbDataObject()
        
        # Преобразуем основные поля
        if orb_data.u:
            proto_obj.u = str(orb_data.u)
        if orb_data.subtype:
            proto_obj.subtype = orb_data.subtype
        if orb_data.chain_left:
            proto_obj.chain_left = str(orb_data.chain_left)
        if orb_data.chain_right:
            proto_obj.chain_right = str(orb_data.chain_right)
        if orb_data.parent:
            proto_obj.parent = str(orb_data.parent)
        if orb_data.ctime:
            proto_obj.ctime = orb_data.ctime.isoformat()
        if orb_data.flags:
            proto_obj.flags.extend(orb_data.flags)
        if orb_data.src:
            proto_obj.src = orb_data.src

        if orb_data.data:
            if orb_data.subtype == OrbDataSybtypes.Json.value:
                proto_obj.data = json.dumps(orb_data.data).encode('utf-8')
            elif orb_data.subtype == OrbDataSybtypes.Raw.value:
                if isinstance(orb_data.data, bytes):
                    proto_obj.data = orb_data.data
                else:
                    proto_obj.data = str(orb_data.data).encode('utf-8')
            else:
                # По умолчанию как JSON
                proto_obj.data = json.dumps(orb_data.data).encode('utf-8')
        
        return proto_obj
    
    @staticmethod
    def _proto_to_orb_data(proto_obj: ProtoOrbDataObject) -> OrbDataObject:
        u = uuid.UUID(proto_obj.u) if proto_obj.u else None
        chain_left = uuid.UUID(proto_obj.chain_left) if proto_obj.chain_left else None
        chain_right = uuid.UUID(proto_obj.chain_right) if proto_obj.chain_right else None
        parent = uuid.UUID(proto_obj.parent) if proto_obj.parent else None
        ctime = None
        if proto_obj.ctime:
            try:
                ctime = datetime.fromisoformat(proto_obj.ctime.replace('Z', '+00:00'))
            except ValueError:
                ctime = datetime.now(timezone.utc)
        data = None
        if proto_obj.data:
            try:
                if proto_obj.subtype == '@json':
                    data = json.loads(proto_obj.data.decode('utf-8'))
                elif proto_obj.subtype == '@raw':
                    data = proto_obj.data
                else:
                    # По умолчанию пытаемся как JSON
                    try:
                        data = json.loads(proto_obj.data.decode('utf-8'))
                    except:
                        data = proto_obj.data
            except Exception:
                data = proto_obj.data
        
        return OrbDataObject(
            u=u,
            src=proto_obj.src or None,
            data=data,
            chain_left=chain_left,
            chain_right=chain_right,
            parent=parent,
            ctime=ctime,
            flags=list(proto_obj.flags),
            subtype=proto_obj.subtype or '@json',
            type="@OrbData"
        )
    
    @staticmethod
    def _orb_meta_to_proto(orb_meta: OrbMetaObject) -> ProtoOrbMetaObject:
        proto_obj = ProtoOrbMetaObject()
        
        if orb_meta.id:
            proto_obj.id = orb_meta.id
        if orb_meta.u:
            proto_obj.u = str(orb_meta.u)
        if orb_meta.ctime:
            proto_obj.ctime = orb_meta.ctime.isoformat()
        if orb_meta.type:
            proto_obj.type = orb_meta.type
        if orb_meta.handle:
            proto_obj.handle = orb_meta.handle
        if orb_meta.flags:
            proto_obj.flags.extend(orb_meta.flags)
        
        return proto_obj
    
    @staticmethod
    def _proto_to_orb_meta(proto_obj: ProtoOrbMetaObject) -> OrbMetaObject:
        u = uuid.UUID(proto_obj.u) if proto_obj.u else None
        
        # Парсим время создания
        ctime = None
        if proto_obj.ctime:
            try:
                ctime = datetime.fromisoformat(proto_obj.ctime.replace('Z', '+00:00'))
            except ValueError:
                ctime = datetime.now(timezone.utc)
        
        return OrbMetaObject(
            id=proto_obj.id or 0,
            u=u,
            type=proto_obj.type or "@OrbMeta",
            handle=proto_obj.handle or None,
            ctime=ctime,
            flags=list(proto_obj.flags)
        )
    
    def push_orb_data(self, orb_data: OrbDataObject) -> OrbDataObject:
        proto_data = self._orb_data_to_proto(orb_data)
        request = OrbDataRequest(data=proto_data)
        response = self.stub.PushOrbData(request)
        
        if not response.success:
            raise Exception(f"Failed to push orb data: {response.message}")
        return orb_data
    
    def push_orb_meta(self, orb_meta: OrbMetaObject) -> OrbMetaObject:
        proto_meta = self._orb_meta_to_proto(orb_meta)
        request = OrbMetaRequest(meta=proto_meta)
        response = self.stub.PushOrbMeta(request)
        
        if not response.success:
            raise Exception(f"Failed to push orb meta: {response.message}")

        return orb_meta
    
    def fetch_orb_data(self, identifier: Union[str, uuid.UUID]) -> Optional[OrbDataObject]:
        if isinstance(identifier, uuid.UUID):
            identifier = str(identifier)
        request = FetchOrbRequest(identifier=identifier, type="data")
        response = self.stub.FetchOrbData(request)
        if not response.data.u:
            return None
        return self._proto_to_orb_data(response.data)
    
    def fetch_orb_meta(self, identifier: Union[int, str]) -> Optional[OrbMetaObject]:
        if isinstance(identifier, int):
            identifier = str(identifier)
        request = FetchOrbRequest(identifier=identifier, type="meta")
        response = self.stub.FetchOrbMeta(request)
        if not response.meta.u:
            return None
        return self._proto_to_orb_meta(response.meta)

    def create_orb_data(self, 
                       data: dict, 
                       subtype: str = '@json',
                       src: Optional[str] = None,
                       chain_left: Optional[uuid.UUID] = None,
                       chain_right: Optional[uuid.UUID] = None,
                       parent: Optional[uuid.UUID] = None,
                       flags: Optional[List[str]] = None) -> OrbDataObject:
        orb_data = OrbDataObject(
            u=uuid.uuid1(),
            src=src,
            data=data,
            chain_left=chain_left,
            chain_right=chain_right,
            parent=parent,
            ctime=datetime.now(timezone.utc),
            flags=flags or [],
            subtype=subtype,
            type="@OrbData"
        )
        
        return self.push_orb_data(orb_data)
    
    def create_orb_meta(self,
                       handle: int,
                       obj_type: str = "@OrbMeta",
                       flags: Optional[List[str]] = None) -> OrbMetaObject:
        orb_meta = OrbMetaObject(
            id = 0,  # Сервер присвоит ID
            u=uuid.uuid1(),
            type=obj_type,
            handle=handle,
            ctime=datetime.now(timezone.utc),
            flags=flags or []
        )
        
        return self.push_orb_meta(orb_meta)

    def push_data(self, key: str, data: bytes) -> bool:
        request = PushDataRequest(key=key, data=data)
        response = self.stub.PushData(request)
        return response.success
    
    def push_meta(self, key: str, meta: bytes) -> bool:
        request = PushMetaRequest(key=key, meta=meta)
        response = self.stub.PushMeta(request)
        return response.success
    
    def fetch_data(self, key: str) -> Optional[bytes]:
        request = FetchRequest(key=key)
        response = self.stub.FetchData(request)
        return response.data if response.data else None
    
    def fetch_meta(self, key: str) -> Optional[bytes]:
        request = FetchRequest(key=key)
        response = self.stub.FetchMeta(request)
        return response.meta if response.meta else None

