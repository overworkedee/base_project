""" 系统信息数据模型 """

from PySide6.QtCore import QObject, Signal

from pc_dashboard.protocol.cmd_defs import (
    CMD_SYSTEM,
    CMD_SUB_READ,
    CMD_SUB_SET_LOGLEVEL,
    CMD_ERR_OK,
    LOG_LEVEL_DEBUG,
)


class SystemModel(QObject):
    """ 系统信息 + 日志等级控制。 """

    version_updated = Signal(str)
    loglevel_updated = Signal(int)

    def __init__(self, client):
        super().__init__()
        self._client = client
        self.version = ""
        self.log_level = LOG_LEVEL_DEBUG

    def on_connected(self) -> None:
        """ 连接成功后查询版本。 """
        rsp = self._client.send_request(CMD_SYSTEM, CMD_SUB_READ)
        if rsp and rsp.get("payload") and rsp["payload"][0] == CMD_ERR_OK:
            self.version = bytes(rsp["payload"][1:]).decode("utf-8", errors="replace")
            self.version_updated.emit(self.version)

    def set_log_level(self, level: int) -> bool:
        """ 设置日志等级 (0=DEBUG, 1=INFO, 2=WARN, 3=ERROR)。 """
        rsp = self._client.send_request(CMD_SYSTEM, CMD_SUB_SET_LOGLEVEL, bytes([level]))
        if rsp and rsp.get("payload") and rsp["payload"][0] == CMD_ERR_OK:
            self.log_level = level
            self.loglevel_updated.emit(level)
            return True
        return False
