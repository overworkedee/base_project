""" 主窗口 —— 连接栏 + Tab 容器 + 状态栏 """

from PySide6.QtWidgets import (
    QMainWindow, QWidget, QHBoxLayout, QVBoxLayout, QLineEdit,
    QPushButton, QLabel, QTabWidget, QStatusBar, QApplication,
)

from pc_dashboard.protocol.cmd_client import CmdClient
from pc_dashboard.models.sensor_model import SensorModel
from pc_dashboard.models.led_model import LedModel
from pc_dashboard.models.system_model import SystemModel
from pc_dashboard.models.log_model import LogModel
from pc_dashboard.gui.dashboard_tab import DashboardTab
from pc_dashboard.gui.log_tab import LogTab
from pc_dashboard.gui.system_tab import SystemTab
from pc_dashboard.gui.terminal_tab import TerminalTab

CONNECTED_STYLE = "color: #27ae60; font-weight: bold;"
DISCONNECTED_STYLE = "color: #e74c3c; font-weight: bold;"


class MainWindow(QMainWindow):
    """ PC Dashboard 主窗口。 """

    def __init__(self):
        super().__init__()
        self.setWindowTitle("PC Dashboard")
        self.resize(900, 650)

        # 协议层
        self.client = CmdClient()

        # 数据模型
        self.sensor_model = SensorModel(self.client)
        self.led_model = LedModel(self.client)
        self.system_model = SystemModel(self.client)
        self.log_model = LogModel(self.client)

        # 推送路由
        self.client.set_push_callback(self._on_push)

        # 连接断开回调
        self.client.connection_lost.connect(self._on_connection_lost)

        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self) -> None:
        """ 构建 UI 布局。 """
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(10, 10, 10, 10)

        # ── 顶部连接栏 ──
        conn_layout = QHBoxLayout()

        conn_layout.addWidget(QLabel("Host:"))
        self.host_input = QLineEdit("192.168.3.171")
        self.host_input.setFixedWidth(140)
        conn_layout.addWidget(self.host_input)

        conn_layout.addWidget(QLabel("Port:"))
        self.port_input = QLineEdit("9527")
        self.port_input.setFixedWidth(60)
        conn_layout.addWidget(self.port_input)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._on_connect)
        conn_layout.addWidget(self.connect_btn)

        self.reconnect_btn = QPushButton("Reconnect")
        self.reconnect_btn.clicked.connect(self._on_connect)
        self.reconnect_btn.setEnabled(False)
        conn_layout.addWidget(self.reconnect_btn)

        conn_layout.addStretch()

        self.status_label = QLabel("⚫ Disconnected")
        self.status_label.setStyleSheet(DISCONNECTED_STYLE)
        conn_layout.addWidget(self.status_label)

        main_layout.addLayout(conn_layout)

        # ── Tab 容器 ──
        self.tabs = QTabWidget()

        self.dashboard_tab = DashboardTab(self.sensor_model, self.led_model)
        self.log_tab = LogTab(self.log_model)
        self.system_tab = SystemTab(self.system_model)
        self.terminal_tab = TerminalTab()

        self.tabs.addTab(self.dashboard_tab, "仪表盘")
        self.tabs.addTab(self.log_tab, "日志")
        self.tabs.addTab(self.system_tab, "系统")
        self.tabs.addTab(self.terminal_tab, "终端")

        main_layout.addWidget(self.tabs, stretch=1)

        # ── 底部状态栏 ──
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("未连接")

    def _connect_signals(self) -> None:
        """ 连接数据模型信号到状态栏更新。 """
        self.sensor_model.temperature_updated.connect(
            lambda v: self._update_status_summary()
        )
        self.sensor_model.humidity_updated.connect(
            lambda v: self._update_status_summary()
        )

    def _on_connect(self) -> None:
        """ 连接/重连按钮回调。 """
        host = self.host_input.text().strip()
        try:
            port = int(self.port_input.text().strip())
        except ValueError:
            self.status_bar.showMessage("端口号无效")
            return

        self.status_bar.showMessage(f"正在连接 {host}:{port} ...")
        QApplication.processEvents()

        if self.client.connect(host, port):
            self.status_label.setText("● Connected")
            self.status_label.setStyleSheet(CONNECTED_STYLE)
            self.connect_btn.setEnabled(False)
            self.reconnect_btn.setEnabled(True)
            self.status_bar.showMessage(f"已连接 {host}:{port}")

            # 更新系统页连接信息
            self.system_tab.update_connection_info(host, port, True)

            # 通知各模型连接成功
            self.sensor_model.on_connected()
            self.led_model.on_connected()
            self.system_model.on_connected()
            self.log_model.on_connected()
        else:
            self.status_label.setText("⚫ Disconnected")
            self.status_bar.showMessage("连接失败")

    def _on_connection_lost(self) -> None:
        """ 连接断开的回调。 """
        self.status_label.setText("⚫ Disconnected")
        self.status_label.setStyleSheet(DISCONNECTED_STYLE)
        self.connect_btn.setEnabled(True)
        self.reconnect_btn.setEnabled(False)
        self.status_bar.showMessage("连接断开")

        self.system_tab.update_connection_info("", 0, False)
        self.sensor_model.on_disconnected()
        self.led_model.on_disconnected()
        self.log_model.on_disconnected()

    def _on_push(self, frame: dict) -> None:
        """ 推送帧分发到各模型。 """
        self.sensor_model.handle_push(frame)
        self.log_model.handle_push(frame)

    def _update_status_summary(self) -> None:
        """ 更新底部状态栏摘要。 """
        if self.client.is_connected:
            self.status_bar.showMessage(
                f"已连接 | 温度: {self.sensor_model.temperature:.1f}°C "
                f"| 湿度: {self.sensor_model.humidity:.1f}%"
            )

    def closeEvent(self, event) -> None:
        """ 窗口关闭时断开连接。 """
        self.client.disconnect()
        self.terminal_tab._worker.disconnect()
        super().closeEvent(event)
