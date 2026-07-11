""" 日志流标签页 —— 等级过滤 + 彩色标签 + 暂停/自动滚动 """

from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QTableWidget,
    QTableWidgetItem, QComboBox, QPushButton, QLabel, QHeaderView,
)
from PySide6.QtGui import QColor

from pc_dashboard.protocol.cmd_defs import (
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
)

_LEVEL_COLORS = {
    LOG_LEVEL_DEBUG: QColor("#95a5a6"),
    LOG_LEVEL_INFO:  QColor("#2980b9"),
    LOG_LEVEL_WARN:  QColor("#f39c12"),
    LOG_LEVEL_ERROR: QColor("#e74c3c"),
}

FILTERS = [
    ("ALL",   LOG_LEVEL_DEBUG),
    ("INFO+", LOG_LEVEL_INFO),
    ("WARN+", LOG_LEVEL_WARN),
    ("ERROR", LOG_LEVEL_ERROR),
]


class LogTab(QWidget):
    """ 日志流页面。 """

    def __init__(self, log_model):
        super().__init__()
        self._log = log_model
        self._paused = False
        self._auto_scroll = True

        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)

        # ── 工具栏 ──
        toolbar = QHBoxLayout()

        toolbar.addWidget(QLabel("等级过滤:"))
        self.filter_combo = QComboBox()
        for label, _ in FILTERS:
            self.filter_combo.addItem(label)
        self.filter_combo.currentIndexChanged.connect(self._on_filter_changed)
        toolbar.addWidget(self.filter_combo)

        self.clear_btn = QPushButton("清空")
        self.clear_btn.clicked.connect(self._log.clear)
        toolbar.addWidget(self.clear_btn)

        self.pause_btn = QPushButton("暂停")
        self.pause_btn.setCheckable(True)
        self.pause_btn.toggled.connect(self._on_pause)
        toolbar.addWidget(self.pause_btn)

        toolbar.addStretch()

        self.count_label = QLabel("共 0 条")
        toolbar.addWidget(self.count_label)

        layout.addLayout(toolbar)

        # ── 日志表格 ──
        self.table = QTableWidget()
        self.table.setColumnCount(3)
        self.table.setHorizontalHeaderLabels(["时间", "等级", "消息"])
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(1, QHeaderView.ResizeToContents)
        self.table.horizontalHeader().setSectionResizeMode(2, QHeaderView.Stretch)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setShowGrid(False)
        self.table.verticalHeader().setVisible(False)
        self.table.setAlternatingRowColors(True)

        self.table.verticalScrollBar().valueChanged.connect(self._on_scroll)

        layout.addWidget(self.table)

    def _connect_signals(self) -> None:
        self._log.log_received.connect(self._on_log)
        self._log.log_cleared.connect(self._on_cleared)

    def _on_log(self, level: int, ts: str, msg: str) -> None:
        """ 收到一条日志。 """
        if level < self._log.filter_level:
            return

        row = self.table.rowCount()
        self.table.insertRow(row)

        time_item = QTableWidgetItem(ts)
        self.table.setItem(row, 0, time_item)

        lvl_name = self._log.level_name(level)
        lvl_item = QTableWidgetItem(lvl_name)
        lvl_item.setForeground(_LEVEL_COLORS.get(level, QColor("#000000")))
        self.table.setItem(row, 1, lvl_item)

        msg_item = QTableWidgetItem(msg)
        self.table.setItem(row, 2, msg_item)

        if not self._paused and self._auto_scroll:
            self.table.scrollToBottom()

        self._update_count()

    def _on_cleared(self) -> None:
        self.table.setRowCount(0)
        self._update_count()

    def _on_filter_changed(self, index: int) -> None:
        _, level = FILTERS[index]
        self._log.set_filter(level)
        self._rebuild_table()

    def _on_pause(self, checked: bool) -> None:
        self._paused = checked
        self.pause_btn.setText("继续" if checked else "暂停")
        if not checked:
            self._rebuild_table()
            self.table.scrollToBottom()

    def _on_scroll(self, value: int) -> None:
        scrollbar = self.table.verticalScrollBar()
        if scrollbar.maximum() - value > 20:
            self._auto_scroll = False
        else:
            self._auto_scroll = True

    def _rebuild_table(self) -> None:
        self.table.setRowCount(0)
        for level, ts, msg in self._log.buffer.get_all():
            if level >= self._log.filter_level:
                row = self.table.rowCount()
                self.table.insertRow(row)

                time_item = QTableWidgetItem(ts)
                self.table.setItem(row, 0, time_item)

                lvl_item = QTableWidgetItem(self._log.level_name(level))
                lvl_item.setForeground(_LEVEL_COLORS.get(level, QColor("#000000")))
                self.table.setItem(row, 1, lvl_item)

                self.table.setItem(row, 2, QTableWidgetItem(msg))

        self._update_count()

    def _update_count(self) -> None:
        total = len(self._log.buffer)
        shown = self.table.rowCount()
        self.count_label.setText(f"共 {total} 条，显示 {shown} 条")
