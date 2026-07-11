""" SSH 终端标签页 —— 命令执行 + 输出显示 """

import threading
import re

import paramiko
from PySide6.QtCore import QObject, Signal
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QPlainTextEdit, QLineEdit, QPushButton, QLabel, QApplication,
)
from PySide6.QtGui import QFont, QTextCursor

# ── ANSI 过滤 ──────────────────────────────────────────────────────

_ANSI_RE = re.compile(r'\x1B(?:][^\x07]*\x07|\[[0-?]*[ -/]*[@-~]|[@-Z\\-_])')


def _clean(text: str) -> str:
    """ 过滤 ANSI 转义序列 + 清理控制字符。 """
    text = _ANSI_RE.sub('', text)
    # 去掉 \r 和孤立 \b
    text = text.replace('\r', '')
    # 折叠连续空白行
    text = re.sub(r'\n{3,}', '\n\n', text)
    return text


# ── SSH 执行线程 ───────────────────────────────────────────────────

class _SshWorker(QObject):
    """ SSH 后台工作线程——连接 + 执行命令。 """

    command_output = Signal(str)       # 命令输出
    connection_changed = Signal(bool)  # 连接状态
    error_occurred = Signal(str)       # 错误

    def __init__(self):
        super().__init__()
        self._client: paramiko.SSHClient | None = None
        self._lock = threading.Lock()

    def connect(self, host, port, username, password):
        """ 阻塞建立 SSH 连接（在后台线程中调用）。 """
        self.disconnect()
        try:
            client = paramiko.SSHClient()
            client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            client.connect(
                host, port=port,
                username=username, password=password,
                timeout=5, allow_agent=False, look_for_keys=False,
            )
            with self._lock:
                self._client = client
            self.connection_changed.emit(True)
        except Exception as e:
            self.error_occurred.emit(f"SSH 连接失败: {e}")

    def disconnect(self):
        """ 断开连接。 """
        with self._lock:
            if self._client:
                try:
                    self._client.close()
                except Exception:
                    pass
                self._client = None
        self.connection_changed.emit(False)

    def exec_command(self, cmd: str):
        """ 执行命令并通过 signal 返回输出（在后台线程中调用）。 """
        with self._lock:
            client = self._client
        if not client:
            self.error_occurred.emit("未连接")
            return

        try:
            stdin, stdout, stderr = client.exec_command(cmd, timeout=10)
            out = stdout.read().decode('utf-8', errors='replace')
            err = stderr.read().decode('utf-8', errors='replace')
            if err:
                out += err
            self.command_output.emit(out)
        except Exception as e:
            self.error_occurred.emit(f"执行失败: {e}")


# ── 终端 Tab ──────────────────────────────────────────────────────

class TerminalTab(QWidget):
    """
    SSH 命令终端 —— 输入命令 → exec_command 执行 → 显示输出。

    不是全 PTY 终端（无 Tab 补全/vi 等），而是命令执行器。
    适合 ls / pwd / cat / dmesg / systemctl 等一次性命令。
    """

    def __init__(self):
        super().__init__()
        self._worker = _SshWorker()
        self._connected = False
        self._host = "192.168.3.171"
        self._user = "orangepi"

        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        # ── 连接栏 ──
        bar = QHBoxLayout()

        bar.addWidget(QLabel("Host:"))
        self.host = QLineEdit("192.168.3.171")
        self.host.setFixedWidth(130)
        bar.addWidget(self.host)

        bar.addWidget(QLabel("User:"))
        self.user = QLineEdit("orangepi")
        self.user.setFixedWidth(80)
        bar.addWidget(self.user)

        bar.addWidget(QLabel("Pwd:"))
        self.pwd = QLineEdit("orangepi")
        self.pwd.setEchoMode(QLineEdit.Password)
        self.pwd.setFixedWidth(80)
        bar.addWidget(self.pwd)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._on_connect)
        bar.addWidget(self.connect_btn)

        self.disconnect_btn = QPushButton("Disconnect")
        self.disconnect_btn.clicked.connect(self._on_disconnect)
        self.disconnect_btn.setEnabled(False)
        bar.addWidget(self.disconnect_btn)

        bar.addStretch()
        self.status_lbl = QLabel("⚫")
        bar.addWidget(self.status_lbl)
        layout.addLayout(bar)

        # ── 输出区 ──
        self.term = QPlainTextEdit()
        self.term.setReadOnly(True)
        self.term.setFont(QFont("Courier New", 11))
        self.term.setStyleSheet("""
            QPlainTextEdit {
                background-color: #1e1e1e;
                color: #d4d4d4;
                border: 1px solid #3c3c3c;
            }
        """)
        layout.addWidget(self.term)

        # ── 输入栏 ──
        cmd_bar = QHBoxLayout()
        self.prompt_lbl = QLabel("$")
        self.prompt_lbl.setStyleSheet("color: #27ae60; font-weight: bold; font-size: 14px;")
        cmd_bar.addWidget(self.prompt_lbl)

        self.cmd_input = QLineEdit()
        self.cmd_input.setPlaceholderText("输入命令，Enter 执行...")
        self.cmd_input.returnPressed.connect(self._on_send)
        cmd_bar.addWidget(self.cmd_input)

        send_btn = QPushButton("执行")
        send_btn.clicked.connect(self._on_send)
        cmd_bar.addWidget(send_btn)

        layout.addLayout(cmd_bar)

    def _connect_signals(self):
        self._worker.command_output.connect(self._on_output)
        self._worker.connection_changed.connect(self._on_connection)
        self._worker.error_occurred.connect(self._on_error)

    # ── 槽 ────────────────────────────────────────────────────────

    def _on_connect(self):
        host = self.host.text().strip()
        user = self.user.text().strip()
        pwd = self.pwd.text().strip()
        self._host = host
        self._user = user

        self.term.clear()
        self.term.appendPlainText(f"Connecting to {user}@{host} ...")
        self.connect_btn.setEnabled(False)
        QApplication.processEvents()

        t = threading.Thread(
            target=self._worker.connect,
            args=(host, 22, user, pwd),
            daemon=True,
        )
        t.start()

    def _on_disconnect(self):
        self._worker.disconnect()
        self.term.appendPlainText("\n--- Disconnected ---")

    def _on_output(self, text: str):
        """ 命令输出追加到终端。 """
        clean = _clean(text)
        if clean:
            self.term.appendPlainText(clean.rstrip())
        # 滚动到底
        sb = self.term.verticalScrollBar()
        sb.setValue(sb.maximum())

    def _on_connection(self, connected: bool):
        self._connected = connected
        if connected:
            self.status_lbl.setText("● 已连接")
            self.status_lbl.setStyleSheet("color: #27ae60; font-weight: bold;")
            self.connect_btn.setEnabled(False)
            self.disconnect_btn.setEnabled(True)
            self.prompt_lbl.setText(f"{self._user}@orangepi$")
            self.term.appendPlainText(f"Connected to {self._user}@{self._host}\n")
            self.cmd_input.setFocus()
            # 获取初始 MOTD
            self._run_in_thread("uname -a; uptime; free -h | head -2")
        else:
            self.status_lbl.setText("⚫ 未连接")
            self.status_lbl.setStyleSheet("color: #95a5a6;")
            self.connect_btn.setEnabled(True)
            self.disconnect_btn.setEnabled(False)
            self.prompt_lbl.setText("$")

    def _on_error(self, msg: str):
        self.term.appendPlainText(f"\n[ERROR] {msg}")
        self.connect_btn.setEnabled(True)

    def _on_send(self):
        """ 执行命令。 """
        cmd = self.cmd_input.text().strip()
        if not cmd:
            return
        # 回显命令
        self.term.appendPlainText(f"\n{self._user}@orangepi$ {cmd}")
        self.cmd_input.clear()
        QApplication.processEvents()

        self._run_in_thread(cmd)

    def _run_in_thread(self, cmd: str):
        """ 在后台线程执行命令，避免阻塞 GUI。 """
        t = threading.Thread(target=self._worker.exec_command, args=(cmd,), daemon=True)
        t.start()
