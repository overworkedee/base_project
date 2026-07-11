""" 系统信息标签页 —— 版本 + 日志等级控制 + 连接统计 """

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QGroupBox, QHBoxLayout,
    QLabel, QRadioButton, QPushButton, QButtonGroup,
)

from pc_dashboard.protocol.cmd_defs import (
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
)

LEVELS = [
    ("DEBUG", LOG_LEVEL_DEBUG),
    ("INFO",  LOG_LEVEL_INFO),
    ("WARN",  LOG_LEVEL_WARN),
    ("ERROR", LOG_LEVEL_ERROR),
]


class SystemTab(QWidget):
    """ 系统页面：版本信息、日志等级控制、连接统计。 """

    def __init__(self, system_model):
        super().__init__()
        self._sys = system_model
        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)

        # ── 系统信息 ──
        info_group = QGroupBox("系统信息")
        info_layout = QVBoxLayout(info_group)

        self.version_label = QLabel("固件版本: ---")
        self.version_label.setStyleSheet("font-size: 14px;")
        info_layout.addWidget(self.version_label)

        self.conn_label = QLabel("连接方式: ---")
        info_layout.addWidget(self.conn_label)

        self.status_label = QLabel("连接状态: ⚫ 未连接")
        info_layout.addWidget(self.status_label)

        layout.addWidget(info_group)

        # ── 日志等级 ──
        log_group = QGroupBox("日志等级")
        log_layout = QVBoxLayout(log_group)

        radio_layout = QHBoxLayout()
        self.level_group = QButtonGroup(self)
        self.radios = {}
        for label, level in LEVELS:
            radio = QRadioButton(label)
            self.radios[level] = radio
            self.level_group.addButton(radio, level)
            radio_layout.addWidget(radio)
        log_layout.addLayout(radio_layout)

        apply_btn = QPushButton("应用")
        apply_btn.clicked.connect(self._on_apply)
        apply_btn.setFixedWidth(80)
        log_layout.addWidget(apply_btn)

        self.level_status = QLabel("")
        self.level_status.setStyleSheet("color: #27ae60;")
        log_layout.addWidget(self.level_status)

        layout.addWidget(log_group)

        # ── 统计 ──
        stat_group = QGroupBox("统计")
        stat_layout = QVBoxLayout(stat_group)
        self.stat_label = QLabel(
            "仪表盘刷新率: --\n日志接收: --\n连接时长: --"
        )
        stat_layout.addWidget(self.stat_label)
        layout.addWidget(stat_group)

        layout.addStretch()

    def _connect_signals(self) -> None:
        self._sys.version_updated.connect(self._on_version)
        self._sys.loglevel_updated.connect(self._on_loglevel)

    def _on_version(self, version: str) -> None:
        self.version_label.setText(f"固件版本: {version}")
        self.status_label.setText("连接状态: ● 已连接")
        self.status_label.setStyleSheet("color: #27ae60;")

    def _on_loglevel(self, level: int) -> None:
        if level in self.radios:
            self.radios[level].setChecked(True)
        self.level_status.setText(
            f"当前等级: {dict(LEVELS).get(level, '?')}  ✓"
        )

    def _on_apply(self) -> None:
        level = self.level_group.checkedId()
        if level >= 0:
            if self._sys.set_log_level(level):
                self.level_status.setText(
                    f"已设置为: {dict(LEVELS).get(level, '?')}  ✓"
                )
                self.level_status.setStyleSheet("color: #27ae60;")
            else:
                self.level_status.setText("设置失败  ✗")
                self.level_status.setStyleSheet("color: #e74c3c;")

    def update_connection_info(self, host: str, port: int, connected: bool) -> None:
        """ 由 MainWindow 调用，更新连接信息。 """
        if connected:
            self.conn_label.setText(f"连接方式: TCP ({host}:{port})")
            self.status_label.setText("连接状态: ● 已连接")
            self.status_label.setStyleSheet("color: #27ae60;")
        else:
            self.conn_label.setText("连接方式: ---")
            self.status_label.setText("连接状态: ⚫ 未连接")
            self.status_label.setStyleSheet("color: #95a5a6;")
