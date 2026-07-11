""" LED 数据模型 """

import time

from PySide6.QtCore import QObject, Signal

from pc_dashboard.protocol.cmd_defs import CMD_LED, CMD_SUB_WRITE, CMD_SUB_READ, CMD_ERR_OK


class LedModel(QObject):
    """ LED 状态模型，支持开关控制和状态查询。 """

    state_changed = Signal(int, bool)  # led_id, is_on

    def __init__(self, client):
        super().__init__()
        self._client = client
        self.led_state: dict = {}
        self._last_action = 0.0
        self._debounce_ms = 0.5

    def on_connected(self) -> None:
        """ 连接成功后刷新 LED 状态。 """
        self.refresh()

    def on_disconnected(self) -> None:
        self.led_state.clear()

    def set_led(self, led_id: int, on: bool) -> bool:
        """
        开/关 LED，通过 CMD_LED + SUB_WRITE 发送命令。

        内置 500ms 防抖，防止连续点击导致命令风暴。

        @param led_id  LED 编号（当前固定为 1）
        @param on      True=开，False=关
        @return        True 成功，False 防抖期内或发送失败
        """
        now = time.monotonic()
        if now - self._last_action < self._debounce_ms:
            return False
        self._last_action = now

        state_byte = 0x01 if on else 0x00
        pl = bytes([led_id, state_byte])
        rsp = self._client.send_request(CMD_LED, CMD_SUB_WRITE, pl)
        if rsp and rsp.get("payload") and rsp["payload"][0] == CMD_ERR_OK:
            self.led_state[led_id] = on
            self.state_changed.emit(led_id, on)
            return True
        return False

    def refresh(self) -> bool:
        """ 查询 LED 状态（led_id=1）。 """
        rsp = self._client.send_request(CMD_LED, CMD_SUB_READ, bytes([1]))
        if rsp and rsp.get("payload") and rsp["payload"][0] == CMD_ERR_OK:
            pl = rsp["payload"]
            if len(pl) >= 3:
                led_id = pl[1]
                is_on = pl[2] != 0
                self.led_state[led_id] = is_on
                self.state_changed.emit(led_id, is_on)
                return True
        return False
