""" 日志数据模型 —— 环形缓冲 + 等级过滤 """

import struct
from datetime import datetime

from PySide6.QtCore import QObject, Signal

from pc_dashboard.protocol.cmd_defs import (
    CMD_SYSTEM,
    CMD_SUB_LOG_SUBSCRIBE,
    CMD_DATA_LOG,
    LOG_LEVEL_DEBUG,
)
from pc_dashboard.utils.ring_buffer import RingBuffer

_LOG_LEVEL_NAMES = {
    LOG_LEVEL_DEBUG: "DEBUG",
    1: "INFO",
    2: "WARN",
    3: "ERROR",
}


class LogModel(QObject):
    """ 日志流模型。自动订阅日志推送，维护环形缓冲。 """

    log_received = Signal(int, str, str)  # level, timestamp_str, message
    log_cleared = Signal()

    def __init__(self, client):
        super().__init__()
        self._client = client
        self.buffer = RingBuffer(5000)
        self.filter_level = LOG_LEVEL_DEBUG
        self._subscribed = False
        self.error_count = 0

    def on_connected(self) -> None:
        """ 连接成功后订阅日志推送。 """
        if self._client.subscribe(CMD_DATA_LOG, 0):
            self._subscribed = True
        else:
            print("LogModel: failed to subscribe to log stream")

    def on_disconnected(self) -> None:
        self._subscribed = False

    def handle_push(self, frame: dict) -> bool:
        """
        处理日志推送帧。返回 True 表示已处理。

        推送帧 PAYLOAD = [data_id 2B BE, level 1B, reserved 1B, timestamp 4B LE, msg N B]
        """
        sub = frame["sub"] & 0x7F
        if frame["cmd"] != CMD_SYSTEM or sub != CMD_SUB_LOG_SUBSCRIBE:
            return False

        pl = frame.get("payload") or b""
        if len(pl) < 4:
            return False

        data_id = struct.unpack(">H", pl[0:2])[0]
        if data_id != CMD_DATA_LOG:
            return False

        value = pl[2:]
        if len(value) < 6:
            self.error_count += 1
            return False

        level = value[0]
        timestamp = struct.unpack("<I", value[2:6])[0]
        msg = value[6:].decode("utf-8", errors="replace")

        ts_str = datetime.fromtimestamp(timestamp).strftime("%H:%M:%S")

        self.buffer.push((level, ts_str, msg))
        self.log_received.emit(level, ts_str, msg)
        return True

    def set_filter(self, level: int) -> None:
        """ 设置最低显示等级。 """
        self.filter_level = level

    def clear(self) -> None:
        """ 清空缓冲区。 """
        self.buffer.clear()
        self.log_cleared.emit()

    @property
    def subscribed(self) -> bool:
        return self._subscribed

    @staticmethod
    def level_name(level: int) -> str:
        return _LOG_LEVEL_NAMES.get(level, "?????")
