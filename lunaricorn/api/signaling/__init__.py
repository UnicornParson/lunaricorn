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

class SignalingEventFlags(Enum):
    JobEntry = "JobEntry"
    JobExit = "JobExit"
    News = "News"
    Md = "Md"
    Obsidian = "Obsidian"

__all__ = ["SignalingEventType", "SignalingClientConfig", "ClientEventData", "SignalingEventFlags"]