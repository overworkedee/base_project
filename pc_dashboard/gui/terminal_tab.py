""" SSH 终端标签页 —— 仿真终端，按键直接发送到远程 shell """

import threading
import re

import paramiko

# ── ANSI 转义序列过滤器 ──────────────────────────────────────────

# 匹配 ANSI 转义序列：OSC (\x1b]...\x07) / CSI (\x1b[...m) / 单字符 (\x1bM 等)
# OSC 必须在前，否则 ] 被 [@-Z] 误匹配为单字符序列
_ANSI_RE = re.compile(r'\x1B(?:][^\x07]*\x07|\[[0-?]*[ -/]*[@-~]|[@-Z\\-_])')


def _strip_ansi(text: str) -> str:
    """ 过滤 ANSI 转义序列（颜色、光标移动等），保留可打印文本。 """
    return _ANSI_RE.sub('', text)
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
        """ 后台连接。先静默关闭旧连接（不 emit 信号），再启动新线程。 """
        self._close_quiet()
        self._running = True
        self._thread = threading.Thread(
            target=self._connect_thread,
            args=(host, port, username, password),
            daemon=True,
        )
        self._thread.start()

    def disconnect(self):
        """ 用户主动断开——关闭连接并通知 GUI。 """
        self._close_quiet()
        self.connection_changed.emit(False)

    def _close_quiet(self):
        """ 内部：静默清理旧连接（不发信号），线程安全。 """
        self._running = False
        if self._channel:
            try: self._channel.close()
            except Exception: pass
            self._channel = None
        if self._client:
            try: self._client.close()
            except Exception: pass
            self._client = None

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
            # 不设 timeout，用 recv_ready() 轮询替代，避免超时异常
            self.connection_changed.emit(True)
        except Exception as e:
            self.error_occurred.emit(f"SSH 连接失败: {e}")
            self._running = False
            return

        # 循环读取: 用 recv_ready() 轮询 + sleep，不依赖 settimeout
        import time as _time
        buf = bytearray()
        while self._running and self._channel and not self._channel.closed:
            try:
                if self._channel.recv_ready():
                    chunk = self._channel.recv(4096)
                    if chunk:
                        buf.extend(chunk)
                        try:
                            text = buf.decode('utf-8', errors='replace')
                            if text:
                                clean = _strip_ansi(text)
                                if clean:
                                    self.output_received.emit(clean)
                            buf.clear()
                        except Exception:
                            pass
                    else:
                        break  # 空 chunk = 通道关闭
                else:
                    _time.sleep(0.05)  # 无数据，短暂休眠
            except Exception as e:
                self.error_occurred.emit(f"读取错误: {type(e).__name__}: {e}")
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
        if obj is not self.term:
            return super().eventFilter(obj, event)

        # 未连接时允许本地编辑（输入连接信息测试用）
        if not self._connected:
            return super().eventFilter(obj, event)

        # 已连接：所有按键转发到 SSH，禁止 QPlainTextEdit 本地插入
        if event.type() == event.Type.KeyPress:
            self._handle_key(event)
            return True  # 始终吞掉事件，不交给 QPlainTextEdit

        # 鼠标选择/复制可以放行
        if event.type() in (event.Type.MouseButtonPress,
                            event.Type.MouseButtonRelease,
                            event.Type.MouseMove):
            return super().eventFilter(obj, event)

        # 其他事件（快捷键等）也放行
        return super().eventFilter(obj, event)

    def _handle_key(self, event: QKeyEvent):
        """ 将按键转发到 SSH PTY。直接通过 channel.send 发送字节。 """
        key = event.key()
        modifiers = event.modifiers()

        # Ctrl 组合键
        if modifiers == Qt.ControlModifier:
            ctrl_map = {
                Qt.Key_C: b'\x03', Qt.Key_D: b'\x04', Qt.Key_Z: b'\x1a',
                Qt.Key_L: b'\x0c', Qt.Key_A: b'\x01', Qt.Key_E: b'\x05',
                Qt.Key_U: b'\x15', Qt.Key_W: b'\x17', Qt.Key_K: b'\x0b',
            }
            if key in ctrl_map:
                self._worker.send(ctrl_map[key])
                return

        # 功能键
        key_map = {
            Qt.Key_Return:   b'\r',
            Qt.Key_Enter:    b'\r',
            Qt.Key_Backspace: b'\x7f',
            Qt.Key_Tab:      b'\t',
            Qt.Key_Escape:   b'\x1b',
            Qt.Key_Up:       b'\x1b[A',
            Qt.Key_Down:     b'\x1b[B',
            Qt.Key_Right:    b'\x1b[C',
            Qt.Key_Left:     b'\x1b[D',
            Qt.Key_Home:     b'\x1b[H',
            Qt.Key_End:      b'\x1b[F',
            Qt.Key_Delete:   b'\x1b[3~',
        }
        if key in key_map:
            self._worker.send(key_map[key])
            return

        # PageUp/PageDown
        if key == Qt.Key_PageUp:
            self._worker.send(b'\x1b[5~')
            return
        if key == Qt.Key_PageDown:
            self._worker.send(b'\x1b[6~')
            return

        # 普通可打印字符（含中文等多字节字符）
        text = event.text()
        if text:
            self._worker.send(text.encode('utf-8'))
        # 未识别的键：静默忽略

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
