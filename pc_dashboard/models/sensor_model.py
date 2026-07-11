""" 传感器数据模型 —— 温湿度缓存 + 订阅管理 """

import struct

from PySide6.QtCore import QObject, Signal

from pc_dashboard.protocol.cmd_defs import (
    CMD_SENSOR,
    CMD_SUB_READ,
    CMD_SUB_SUBSCRIBE,
    CMD_DATA_TEMPERATURE,
    CMD_DATA_HUMIDITY,
    CMD_ERR_OK,
)


class SensorModel(QObject):
    """ 温湿度数据模型。连接成功后自动读取初始值并订阅推送。 """

    temperature_updated = Signal(float)
    humidity_updated = Signal(float)
    subscription_changed = Signal(int, bool)  # data_id, is_subscribed

    def __init__(self, client):
        super().__init__()
        self._client = client
        self.temperature = 0.0
        self.humidity = 0.0
        self._subscribed: set = set()

    def on_connected(self) -> None:
        """
        连接成功后调用。

        发送 CMD_SENSOR + SUB_READ 读取初始温湿度，然后自动订阅 1 秒推送。
        结果通过 temperature_updated / humidity_updated signals 通知 GUI。
        """
        # 读取初始值
        rsp = self._client.send_request(CMD_SENSOR, CMD_SUB_READ)
        if rsp and rsp.get("payload"):
            pl = rsp["payload"]
            if pl[0] == CMD_ERR_OK and rsp["len"] >= 9:
                temp_raw = struct.unpack(">I", pl[1:5])[0]
                hum_raw = struct.unpack(">I", pl[5:9])[0]
                self.temperature = struct.unpack("!f", struct.pack(">I", temp_raw))[0]
                self.humidity = struct.unpack("!f", struct.pack(">I", hum_raw))[0]
                self.temperature_updated.emit(self.temperature)
                self.humidity_updated.emit(self.humidity)

        # 订阅温度推送
        if self._client.subscribe(CMD_DATA_TEMPERATURE, 1000):
            self._subscribed.add(CMD_DATA_TEMPERATURE)
            self.subscription_changed.emit(CMD_DATA_TEMPERATURE, True)

        # 订阅湿度推送
        if self._client.subscribe(CMD_DATA_HUMIDITY, 1000):
            self._subscribed.add(CMD_DATA_HUMIDITY)
            self.subscription_changed.emit(CMD_DATA_HUMIDITY, True)

    def on_disconnected(self) -> None:
        """ 连接断开后调用：清除订阅标记。 """
        self._subscribed.clear()

    def handle_push(self, frame: dict) -> bool:
        """
        处理传感器推送帧（从后台线程通过 MainWindow._on_push 分发）。

        推送帧格式: PAYLOAD=[data_id 2B BE, value 4B BE float]
        成功解析后 emit temperature_updated 或 humidity_updated。

        @param frame  解析后的帧 dict
        @return       True 表示已处理，False 表示非传感器推送帧
        """
        sub = frame["sub"] & 0x7F
        if frame["cmd"] != CMD_SENSOR or sub != CMD_SUB_SUBSCRIBE:
            return False

        pl = frame.get("payload") or b""
        if len(pl) < 4:
            return False

        data_id = struct.unpack(">H", pl[0:2])[0]
        value_bytes = pl[2:]

        if data_id == CMD_DATA_TEMPERATURE and len(value_bytes) == 4:
            raw = struct.unpack(">I", value_bytes)[0]
            self.temperature = struct.unpack("!f", struct.pack(">I", raw))[0]
            self.temperature_updated.emit(self.temperature)
            return True

        elif data_id == CMD_DATA_HUMIDITY and len(value_bytes) == 4:
            raw = struct.unpack(">I", value_bytes)[0]
            self.humidity = struct.unpack("!f", struct.pack(">I", raw))[0]
            self.humidity_updated.emit(self.humidity)
            return True

        return False
