""" SSH 终端标签页 —— 通过 paramiko 连接开发板执行命令 """

import threading

import paramiko
from PySide6.QtCore import QObject, Signal
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QPlainTextEdit, QLineEdit, QPushButton,
    QLabel, QApplication,
)
from PySide6.QtGui import QFont, QTextCursor, QColor


class _SshWorker(QObject):
    """ SSH 后台工作线程，负责连接、读取输出、发送命令。 """

    output_received = Signal(str)       # 终端输出文本
    connection_changed = Signal(bool)   # 连接状态
    error_occurred = Signal(str)        # 错误消息

    def __init__(self):
        super().__init__()
        self._client: paramiko.SSHClient | None = None
        self._channel: paramiko.Channel | None = None
        self._running = False
        self._thread: threading.Thread | None = None

    def connect(self, host: str, port: int, username: str, password: str) -> None:
        """ 启动后台连接线程。 """
        self._running = True
        self._thread = threading.Thread(
            target=self._connect_thread,
            args=(host, port, username, password),
            daemon=True,
        )
        self._thread.start()

    def disconnect(self) -> None:
        """ 断开 SSH 连接。 """
        self._running = False
        if self._channel:
            try:
                self._channel.close()
            except Exception:
                pass
            self._channel = None
        if self._client:
            try:
                self._client.close()
            except Exception:
                pass
            self._client = None
        self.connection_changed.emit(False)

    def send_command(self, cmd: str) -> None:
        """ 向 SSH 通道发送命令。 """
        if self._channel and self._channel.active:
            try:
                self._channel.send(cmd + "\n")
            except Exception as e:
                self.error_occurred.emit(f"发送失败: {e}")

    def _connect_thread(self, host: str, port: int, username: str, password: str) -> None:
        """ 后台线程：建立 SSH 连接并循环读取输出。 """
        try:
            self._client = paramiko.SSHClient()
            self._client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            self._client.connect(
                host, port=port,
                username=username, password=password,
                timeout=5, allow_agent=False, look_for_keys=False,
            )
            self._channel = self._client.invoke_shell(
                term='xterm-256color', width=120, height=40,
            )
            self._channel.settimeout(0.5)
            self.connection_changed.emit(True)
        except Exception as e:
            self.error_occurred.emit(f"SSH 连接失败: {e}")
            self._running = False
            return

        # 循环读取 SSH 输出
        buf = bytearray()
        while self._running and self._channel and not self._channel.closed:
            try:
                chunk = self._channel.recv(4096)
                if chunk:
                    buf.extend(chunk)
                    # 尝试 UTF-8 解码并发送到前端
                    try:
                        text = buf.decode('utf-8', errors='replace')
                        if text:
                            # 去掉 ANSI 转义序列（简单实现）
                            clean = self._strip_ansi(text)
                            self.output_received.emit(clean)
                        buf.clear()
                    except Exception:
                        pass
                else:
                    break
            except paramiko.buffered_pipe.PipeTimeout:
                continue
            except Exception:
                break

        # 通道关闭
        self._running = False
        self.connection_changed.emit(False)

    @staticmethod
    def _strip_ansi(text: str) -> str:
        """ 简单的 ANSI 转义序列过滤器。 """
        import re
        ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
        return ansi_escape.sub('', text)


class TerminalTab(QWidget):
    """ SSH 终端页面。 """

    def __init__(self):
        super().__init__()
        self._worker = _SshWorker()
        self._connected = False

        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)

        # ── 连接栏 ──
        conn_layout = QHBoxLayout()

        conn_layout.addWidget(QLabel("Host:"))
        self.host_input = QLineEdit("192.168.3.171")
        self.host_input.setFixedWidth(130)
        conn_layout.addWidget(self.host_input)

        conn_layout.addWidget(QLabel("User:"))
        self.user_input = QLineEdit("orangepi")
        self.user_input.setFixedWidth(80)
        conn_layout.addWidget(self.user_input)

        conn_layout.addWidget(QLabel("Pwd:"))
        self.pwd_input = QLineEdit("orangepi")
        self.pwd_input.setEchoMode(QLineEdit.Password)
        self.pwd_input.setFixedWidth(80)
        conn_layout.addWidget(self.pwd_input)

        self.connect_btn = QPushButton("Connect")
        self.connect_btn.clicked.connect(self._on_connect)
        conn_layout.addWidget(self.connect_btn)

        self.disconnect_btn = QPushButton("Disconnect")
        self.disconnect_btn.clicked.connect(self._on_disconnect)
        self.disconnect_btn.setEnabled(False)
        conn_layout.addWidget(self.disconnect_btn)

        conn_layout.addStretch()

        self.status_label = QLabel("⚫ 未连接")
        self.status_label.setStyleSheet("color: #95a5a6; font-weight: bold;")
        conn_layout.addWidget(self.status_label)

        layout.addLayout(conn_layout)

        # ── 终端输出区 ──
        self.terminal = QPlainTextEdit()
        self.terminal.setReadOnly(True)
        self.terminal.setFont(QFont("Courier New", 10))
        self.terminal.setStyleSheet("""
            QPlainTextEdit {
                background-color: #1e1e1e;
                color: #d4d4d4;
                border: 1px solid #3c3c3c;
            }
        """)
        layout.addWidget(self.terminal, stretch=1)

        # ── 命令输入栏 ──
        cmd_layout = QHBoxLayout()
        self.cmd_input = QLineEdit()
        self.cmd_input.setPlaceholderText("输入命令，按 Enter 发送...")
        self.cmd_input.returnPressed.connect(self._on_send)
        cmd_layout.addWidget(self.cmd_input)

        send_btn = QPushButton("发送")
        send_btn.clicked.connect(self._on_send)
        cmd_layout.addWidget(send_btn)

        layout.addLayout(cmd_layout)

    def _connect_signals(self) -> None:
        self._worker.output_received.connect(self._on_output)
        self._worker.connection_changed.connect(self._on_connection)
        self._worker.error_occurred.connect(self._on_error)

    def _on_connect(self) -> None:
        """ 建立 SSH 连接。 """
        host = self.host_input.text().strip()
        user = self.user_input.text().strip()
        pwd = self.pwd_input.text().strip()

        self.terminal.clear()
        self.terminal.appendPlainText(f"正在连接 {user}@{host} ...\n")
        QApplication.processEvents()

        self.connect_btn.setEnabled(False)
        self._worker.connect(host, 22, user, pwd)

    def _on_disconnect(self) -> None:
        """ 断开 SSH 连接。 """
        self._worker.disconnect()
        self.terminal.appendPlainText("\n--- 连接已断开 ---\n")

    def _on_output(self, text: str) -> None:
        """ 收到 SSH 输出，追加到终端。 """
        self.terminal.moveCursor(QTextCursor.End)
        self.terminal.insertPlainText(text)
        # 自动滚动到底部
        scrollbar = self.terminal.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())

    def _on_connection(self, connected: bool) -> None:
        """ 连接状态变化。 """
        self._connected = connected
        if connected:
            self.status_label.setText("● 已连接")
            self.status_label.setStyleSheet("color: #27ae60; font-weight: bold;")
            self.connect_btn.setEnabled(False)
            self.disconnect_btn.setEnabled(True)
            self.cmd_input.setFocus()
        else:
            self.status_label.setText("⚫ 未连接")
            self.status_label.setStyleSheet("color: #95a5a6; font-weight: bold;")
            self.connect_btn.setEnabled(True)
            self.disconnect_btn.setEnabled(False)

    def _on_error(self, msg: str) -> None:
        """ 显示错误消息。 """
        self.terminal.appendPlainText(f"\n[ERROR] {msg}\n")
        self.connect_btn.setEnabled(True)

    def _on_send(self) -> None:
        """ 发送命令。 """
        cmd = self.cmd_input.text()
        if not cmd.strip():
            return
        self._worker.send_command(cmd)
        self.cmd_input.clear()
