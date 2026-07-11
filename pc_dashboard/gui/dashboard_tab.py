""" 仪表盘标签页 —— 温湿度卡片 + LED 控制 """

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QGroupBox,
    QPushButton, QLabel, QFrame,
)
from PySide6.QtCore import Qt
from pc_dashboard.protocol.cmd_defs import CMD_DATA_TEMPERATURE

TEMP_NORMAL = "color: #2c3e50;"
TEMP_HOT = "color: #e74c3c; font-weight: bold;"
HUM_NORMAL = "color: #2c3e50;"
HUM_LOW = "color: #f39c12; font-weight: bold;"
CARD_STYLE = """
QFrame#card {
    background: #f8f9fa;
    border: 1px solid #dee2e6;
    border-radius: 10px;
    padding: 15px;
}
"""
SUB_ON = "● 订阅中  "
SUB_OFF = "○ 未订阅  "


class DashboardTab(QWidget):
    """ 仪表盘: 温湿度大字体卡片 + LED 开关控制。 """

    def __init__(self, sensor_model, led_model):
        super().__init__()
        self._sensor = sensor_model
        self._led = led_model
        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)

        # ── 温湿度卡片 ──
        cards_layout = QHBoxLayout()

        # 温度卡片
        temp_card = QFrame()
        temp_card.setObjectName("card")
        temp_card.setStyleSheet(CARD_STYLE)
        temp_layout = QVBoxLayout(temp_card)
        temp_title = QLabel("温度 °C")
        temp_title.setAlignment(Qt.AlignCenter)
        temp_title.setStyleSheet("font-size: 14px; color: #7f8c8d;")
        self.temp_value = QLabel("--.-")
        self.temp_value.setAlignment(Qt.AlignCenter)
        self.temp_value.setStyleSheet("font-size: 48px; " + TEMP_NORMAL)
        self.temp_status = QLabel(SUB_OFF)
        self.temp_status.setAlignment(Qt.AlignCenter)
        self.temp_status.setStyleSheet("color: #95a5a6;")
        temp_layout.addWidget(temp_title)
        temp_layout.addWidget(self.temp_value)
        temp_layout.addWidget(self.temp_status)
        cards_layout.addWidget(temp_card)

        # 湿度卡片
        hum_card = QFrame()
        hum_card.setObjectName("card")
        hum_card.setStyleSheet(CARD_STYLE)
        hum_layout = QVBoxLayout(hum_card)
        hum_title = QLabel("湿度 %RH")
        hum_title.setAlignment(Qt.AlignCenter)
        hum_title.setStyleSheet("font-size: 14px; color: #7f8c8d;")
        self.hum_value = QLabel("--.-")
        self.hum_value.setAlignment(Qt.AlignCenter)
        self.hum_value.setStyleSheet("font-size: 48px; " + HUM_NORMAL)
        self.hum_status = QLabel(SUB_OFF)
        self.hum_status.setAlignment(Qt.AlignCenter)
        self.hum_status.setStyleSheet("color: #95a5a6;")
        hum_layout.addWidget(hum_title)
        hum_layout.addWidget(self.hum_value)
        hum_layout.addWidget(self.hum_status)
        cards_layout.addWidget(hum_card)

        layout.addLayout(cards_layout)

        # ── LED 控制 ──
        led_group = QGroupBox("LED 控制")
        led_layout = QVBoxLayout(led_group)

        led_info = QHBoxLayout()
        led_info.addWidget(QLabel("blue_led"))
        self.led_state_label = QLabel("● OFF")
        self.led_state_label.setStyleSheet("color: #95a5a6; font-weight: bold;")
        led_info.addWidget(self.led_state_label)
        led_info.addStretch()
        led_layout.addLayout(led_info)

        btn_layout = QHBoxLayout()
        self.led_on_btn = QPushButton("ON")
        self.led_on_btn.clicked.connect(lambda: self._set_led(True))
        self.led_off_btn = QPushButton("OFF")
        self.led_off_btn.clicked.connect(lambda: self._set_led(False))
        btn_layout.addWidget(self.led_on_btn)
        btn_layout.addWidget(self.led_off_btn)
        btn_layout.addStretch()
        led_layout.addLayout(btn_layout)

        layout.addWidget(led_group)
        layout.addStretch()

    def _connect_signals(self) -> None:
        self._sensor.temperature_updated.connect(self._on_temp)
        self._sensor.humidity_updated.connect(self._on_hum)
        self._sensor.subscription_changed.connect(self._on_sub)
        self._led.state_changed.connect(self._on_led)

    def _on_temp(self, value: float) -> None:
        self.temp_value.setText(f"{value:.1f}")
        if value > 40.0:
            self.temp_value.setStyleSheet("font-size: 48px; " + TEMP_HOT)
        else:
            self.temp_value.setStyleSheet("font-size: 48px; " + TEMP_NORMAL)

    def _on_hum(self, value: float) -> None:
        self.hum_value.setText(f"{value:.1f}")
        if value < 30.0:
            self.hum_value.setStyleSheet("font-size: 48px; " + HUM_LOW)
        else:
            self.hum_value.setStyleSheet("font-size: 48px; " + HUM_NORMAL)

    def _on_sub(self, data_id: int, is_subscribed: bool) -> None:
        status = SUB_ON if is_subscribed else SUB_OFF
        if data_id == CMD_DATA_TEMPERATURE:
            self.temp_status.setText(status)
            self.temp_status.setStyleSheet(
                "color: #27ae60;" if is_subscribed else "color: #95a5a6;"
            )
        else:
            self.hum_status.setText(status)
            self.hum_status.setStyleSheet(
                "color: #27ae60;" if is_subscribed else "color: #95a5a6;"
            )

    def _on_led(self, led_id: int, is_on: bool) -> None:
        if is_on:
            self.led_state_label.setText("● ON")
            self.led_state_label.setStyleSheet("color: #27ae60; font-weight: bold;")
        else:
            self.led_state_label.setText("● OFF")
            self.led_state_label.setStyleSheet("color: #95a5a6; font-weight: bold;")

    def _set_led(self, on: bool) -> None:
        self._led.set_led(1, on)
