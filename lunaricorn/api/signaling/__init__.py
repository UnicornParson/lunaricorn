from .client import *

from enum import Enum

class SignalingEventType(Enum):
    System = "sys"
    Broadcast = "broadcast"
    Robots = "robots"
    FileOp_new = "FileOp_new"
    FileOp_update = "FileOp_update"
    FileOp_delete = "FileOp_delete"
    FileOp_notify = "FileOp_notify"

__all__ = ["SignalingEventType", "SignalingClientConfig", "ClientEventData"]