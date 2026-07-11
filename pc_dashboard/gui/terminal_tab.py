""" SSH 终端标签页 —— 仿真终端，按键直接发送到远程 shell """

import threading
import re

import paramiko
from PySide6.QtCore import QObject, Signal, Qt, QTimer
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QPlainTextEdit, QPushButton, QLabel, QLineEdit, QApplication,
)
from PySide6.QtGui import QFont, QTextCursor, QKeyEvent, QColor


class _SshWorker(QObject):
    """ SSH 后台线程——建立连接、读取输出、发送按键。 """

    output_received = Signal(str)
    connection_changed = Signal(bool)
    error_occurred = Signal(str)

    def __init__(self):
        super().__init__()
        self._client = None
        self._channel = None
        self._running = False
        self._thread = None

    def connect(self, host, port, username, password):
        """ 后台连接。 """
        self.disconnect()
        self._running = True
        self._thread = threading.Thread(
            target=self._connect_thread,
            args=(host, port, username, password),
            daemon=True,
        )
        self._thread.start()

    def disconnect(self):
        """ 断开。 """
        self._running = False
        if self._channel:
            try: self._channel.close()
            except Exception: pass
            self._channel = None
        if self._client:
            try: self._client.close()
            except Exception: pass
            self._client = None
        self.connection_changed.emit(False)

    def send(self, data: bytes):
        """ 发送原始字节到 SSH 通道。 """
        if self._channel and not self._channel.closed:
            try:
                self._channel.send(data)
            except Exception as e:
                self.error_occurred.emit(f"发送失败: {e}")

    def resize_pty(self, cols: int, rows: int):
        """ 调整 PTY 大小。 """
        if self._channel and not self._channel.closed:
            try:
                self._channel.resize_pty(width=cols, height=rows)
            except Exception:
                pass

    def _connect_thread(self, host, port, username, password):
        """ 后台线程：连接 SSH + invoke_shell + 循环读取。 """
        try:
            self._client = paramiko.SSHClient()
            self._client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            self._client.connect(
                host, port=port,
                username=username, password=password,
                timeout=5, allow_agent=False, look_for_keys=False,
            )
            # invoke_shell 创建一个 PTY，shell 在里面运行
            self._channel = self._client.invoke_shell(
                term='xterm-256color', width=120, height=40,
            )
            self._channel.settimeout(0.3)
            self.connection_changed.emit(True)
        except Exception as e:
            self.error_occurred.emit(f"SSH 连接失败: {e}")
            self._running = False
            return

        # 循环读取
        buf = bytearray()
        while self._running and self._channel and not self._channel.closed:
            try:
                chunk = self._channel.recv(4096)
                if chunk:
                    buf.extend(chunk)
                    try:
                        text = buf.decode('utf-8', errors='replace')
                        if text:
                            self.output_received.emit(text)
                        buf.clear()
                    except Exception:
                        pass
                else:
                    break
            except paramiko.buffered_pipe.PipeTimeout:
                continue
            except Exception:
                break

        self._running = False
        self.connection_changed.emit(False)


class TerminalTab(QWidget):
    """
    SSH 仿真终端 —— 直接在终端区域输入命令，体验类似 xshell/putty。

    实现原理：
      - paramiko invoke_shell() 在远程开一个 PTY（bash）
      - 用户每次按键 → _channel.send(bytes) 发送到 PTY
      - bash 解释按键（包括回显）→ 输出通过 _channel.recv() 读取
      - 输出追加到 QPlainTextEdit 中显示
    """

    def __init__(self):
        super().__init__()
        self._worker = _SshWorker()
        self._connected = False

        # 受保护区域长度：之前的内容不可编辑
        self._locked_pos = 0

        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        # ── 连接栏 ──
        bar = QHBoxLayout()
        bar.addWidget(QLabel("Host:"))
        self.host = QLineEdit_clone("192.168.3.171", 130, bar)

        bar.addWidget(QLabel("User:"))
        self.user = QLineEdit_clone("orangepi", 80, bar)

        bar.addWidget(QLabel("Pwd:"))
        self.pwd = QLineEdit_clone("orangepi", 80, bar)
        self.pwd.setEchoMode(QLineEdit.Password)

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

        # ── 终端区域（可编辑，直接输入） ──
        self.term = QPlainTextEdit()
        self.term.setFont(QFont("Courier New", 11))
        self.term.setStyleSheet("""
            QPlainTextEdit {
                background-color: #1e1e1e;
                color: #d4d4d4;
                border: 1px solid #3c3c3c;
                selection-background-color: #264f78;
            }
        """)
        self.term.setCursorWidth(8)  # 粗光标
        self.term.installEventFilter(self)
        self.term.setFocus()
        layout.addWidget(self.term)

    def _connect_signals(self):
        self._worker.output_received.connect(self._on_output)
        self._worker.connection_changed.connect(self._on_connection)
        self._worker.error_occurred.connect(self._on_error)

    # ── 事件过滤器：拦截终端中的按键 → 发送到 SSH ──────────────

    def eventFilter(self, obj, event):
        if obj is not self.term or not self._connected:
            return super().eventFilter(obj, event)

        if event.type() == event.Type.KeyPress:
            return self._handle_key(event)
        return super().eventFilter(obj, event)

    def _handle_key(self, event: QKeyEvent) -> bool:
        """ 将按键转发到 SSH PTY。返回 True 表示事件已处理。 """
        key = event.key()
        modifiers = event.modifiers()

        # 组合键映射
        if modifiers == Qt.ControlModifier:
            ctrl_map = {
                Qt.Key_C: b'\x03',    # Ctrl+C → SIGINT
                Qt.Key_D: b'\x04',    # Ctrl+D → EOF
                Qt.Key_Z: b'\x1a',    # Ctrl+Z → SIGTSTP
                Qt.Key_L: b'\x0c',    # Ctrl+L → clear screen
                Qt.Key_A: b'\x01',    # Ctrl+A → home
                Qt.Key_E: b'\x05',    # Ctrl+E → end
                Qt.Key_U: b'\x15',    # Ctrl+U → kill line
                Qt.Key_W: b'\x17',    # Ctrl+W → kill word
                Qt.Key_K: b'\x0b',    # Ctrl+K → kill to end
            }
            if key in ctrl_map:
                self._worker.send(ctrl_map[key])
                return True

        # 普通按键映射
        if key in (Qt.Key_Return, Qt.Key_Enter):
            self._worker.send(b'\r')   # CR
            return True
        elif key == Qt.Key_Backspace:
            self._worker.send(b'\x7f')
            return True
        elif key == Qt.Key_Tab:
            self._worker.send(b'\t')
            return True
        elif key == Qt.Key_Escape:
            self._worker.send(b'\x1b')
            return True
        elif key == Qt.Key_Up:
            self._worker.send(b'\x1b[A')
            return True
        elif key == Qt.Key_Down:
            self._worker.send(b'\x1b[B')
            return True
        elif key == Qt.Key_Right:
            self._worker.send(b'\x1b[C')
            return True
        elif key == Qt.Key_Left:
            self._worker.send(b'\x1b[D')
            return True
        elif key == Qt.Key_Home:
            self._worker.send(b'\x1b[H')
            return True
        elif key == Qt.Key_End:
            self._worker.send(b'\x1b[F')
            return True
        elif key == Qt.Key_Delete:
            self._worker.send(b'\x1b[3~')
            return True

        # 普通可打印字符
        text = event.text()
        if text and len(text) == 1 and ord(text) >= 0x20:
            self._worker.send(text.encode('utf-8'))
            return True

        return super().eventFilter(self.term, event)

    # ── 槽函数 ──────────────────────────────────────────────

    def _on_output(self, text: str):
        """ SSH 输出追加到终端。替换 QPlainTextEdit 内容避免光标跳动。 """
        tc = self.term.textCursor()
        tc.movePosition(tc.MoveOperation.End)
        tc.insertText(text)
        # 自动滚动到底部
        sb = self.term.verticalScrollBar()
        sb.setValue(sb.maximum())

    def _on_connection(self, connected: bool):
        self._connected = connected
        if connected:
            self.status_lbl.setText("● 已连接")
            self.status_lbl.setStyleSheet("color: #27ae60; font-weight: bold;")
            self.connect_btn.setEnabled(False)
            self.disconnect_btn.setEnabled(True)
            self.term.setFocus()
            QTimer.singleShot(200, lambda: self._worker.resize_pty(120, 40))
        else:
            self.status_lbl.setText("⚫ 未连接")
            self.status_lbl.setStyleSheet("color: #95a5a6;")
            self.connect_btn.setEnabled(True)
            self.disconnect_btn.setEnabled(False)

    def _on_error(self, msg: str):
        tc = self.term.textCursor()
        tc.movePosition(tc.MoveOperation.End)
        tc.insertText(f"\n[ERROR] {msg}\n")
        self.connect_btn.setEnabled(True)

    def _on_connect(self):
        host = self.host.text().strip()
        user = self.user.text().strip()
        pwd = self.pwd.text().strip()
        self.term.clear()
        tc = self.term.textCursor()
        tc.insertText(f"Connecting to {user}@{host} ...\n")
        self.connect_btn.setEnabled(False)
        self._worker.connect(host, 22, user, pwd)

    def _on_disconnect(self):
        self._worker.disconnect()
        tc = self.term.textCursor()
        tc.movePosition(tc.MoveOperation.End)
        tc.insertText("\n--- Disconnected ---\n")


# ── 辅助：快速创建 QLineEdit ──────────────────────────────────

def QLineEdit_clone(text, width, layout):
    w = QLineEdit(text)
    w.setFixedWidth(width)
    layout.addWidget(w)
    return w
