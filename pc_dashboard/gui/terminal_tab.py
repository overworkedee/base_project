""" SSH 终端标签页 —— 内嵌终端模拟器，效果接近 Xshell/Putty """

import threading
import re

import paramiko
from PySide6.QtCore import QObject, Signal
from PySide6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QPlainTextEdit, QLineEdit, QPushButton, QLabel, QApplication,
)
from PySide6.QtGui import QFont, QTextCursor

# ── 终端缓冲区（解释控制序列）─────────────────────────────────────

class TermBuffer:
    """ 模拟 VT100 终端的二维字符缓冲区。"""

    def __init__(self, cols=120, rows=40):
        self.cols = cols
        self.rows = rows
        self.buf = [[' '] * cols for _ in range(rows)]  # [row][col]
        self.cx = 0  # 光标列
        self.cy = 0  # 光标行

    def __str__(self):
        """ 渲染为纯文本字符串（供 QPlainTextEdit 显示）。"""
        lines = []
        for r in range(self.rows):
            # 去掉行尾空白
            line = ''.join(self.buf[r]).rstrip()
            lines.append(line)
        # 去掉底部空行
        while lines and not lines[-1]:
            lines.pop()
        return '\n'.join(lines) if lines else ''

    def write(self, text: str):
        """ 将文本写入缓冲区，逐字符处理控制序列。"""
        i = 0
        n = len(text)
        while i < n:
            ch = text[i]

            if ch == '\x1b':  # ESC 序列
                i += 1
                if i >= n:
                    break
                next_ch = text[i]
                if next_ch == '[':
                    # CSI 序列: \x1b[params letter
                    i += 1
                    params = ''
                    while i < n and text[i] in '0123456789;?':
                        params += text[i]
                        i += 1
                    if i >= n:
                        break
                    cmd = text[i]
                    self._csi(cmd, params)
                elif next_ch == ']':
                    # OSC 序列: \x1b]...\x07 或 \x1b]...\x1b\\
                    i += 1
                    while i < n:
                        if text[i] == '\x07':
                            break
                        if text[i] == '\x1b' and i+1 < n and text[i+1] == '\\':
                            i += 1
                            break
                        i += 1
                # 其他单字符 ESC（如 \x1bM）: 忽略
            elif ch == '\b':  # BS: 退格
                if self.cx > 0:
                    self.cx -= 1
            elif ch == '\r':  # CR: 回车
                self.cx = 0
            elif ch == '\n':  # LF: 换行
                self.cy += 1
                if self.cy >= self.rows:
                    self._scroll_up()
                    self.cy = self.rows - 1
            elif ch == '\t':  # Tab: 跳到下个 8 列边界
                self.cx = ((self.cx // 8) + 1) * 8
                if self.cx >= self.cols:
                    self.cx = 0
                    self.cy += 1
            elif ord(ch) >= 0x20:  # 可打印字符
                self.buf[self.cy][self.cx] = ch
                self.cx += 1
                if self.cx >= self.cols:
                    self.cx = 0
                    self.cy += 1
                    if self.cy >= self.rows:
                        self._scroll_up()
                        self.cy = self.rows - 1
            # 其他控制字符（0x00-0x1f）: 忽略

            i += 1

    def _csi(self, cmd: str, params: str):
        """ 处理 CSI 控制序列。"""
        nums = [int(x) for x in params.replace('?', '').split(';') if x] if params.strip() else []

        if cmd == 'A':  # 上移
            n = nums[0] if nums else 1
            self.cy = max(0, self.cy - n)
        elif cmd == 'B':  # 下移
            n = nums[0] if nums else 1
            self.cy = min(self.rows - 1, self.cy + n)
        elif cmd == 'C':  # 右移
            n = nums[0] if nums else 1
            self.cx = min(self.cols - 1, self.cx + n)
        elif cmd == 'D':  # 左移
            n = nums[0] if nums else 1
            self.cx = max(0, self.cx - n)
        elif cmd == 'G':  # 设列（1-based）
            self.cx = min(self.cols - 1, (nums[0] - 1) if nums else 0)
        elif cmd in ('H', 'f'):  # 光标定位（row;col, 1-based）
            r = (nums[0] - 1) if len(nums) >= 1 else 0
            c = (nums[1] - 1) if len(nums) >= 2 else 0
            self.cy = max(0, min(self.rows - 1, r))
            self.cx = max(0, min(self.cols - 1, c))
        elif cmd == 'J':  # 清屏
            mode = nums[0] if nums else 0
            if mode == 2:  # 清整个屏幕
                for r in range(self.rows):
                    self.buf[r] = [' '] * self.cols
                self.cx = self.cy = 0
        elif cmd == 'K':  # 清行
            mode = nums[0] if nums else 0
            if mode == 0:  # 光标到行尾
                for c in range(self.cx, self.cols):
                    self.buf[self.cy][c] = ' '
        # m (SGR 颜色), h/l (模式), @ (插入), P (删除) 等: 忽略

    def _scroll_up(self):
        """ 向上滚动一行（顶行丢弃）。"""
        for r in range(self.rows - 1):
            self.buf[r] = self.buf[r + 1]
        self.buf[self.rows - 1] = [' '] * self.cols


# ── SSH 后台 ──────────────────────────────────────────────────────

class _SshWorker(QObject):
    """ SSH 后台线程：连接 + invoke_shell + 循环读取 + 按键转发。"""

    output_updated = Signal()        # 终端缓冲区有更新
    connection_changed = Signal(bool)
    error_occurred = Signal(str)

    def __init__(self):
        super().__init__()
        self._client = None
        self._channel = None
        self._running = False
        self._lock = threading.Lock()
        self.term = TermBuffer(120, 40)

    def connect(self, host, port, username, password):
        """ 阻塞建立 SSH + PTY（在后台线程调用）。"""
        with self._lock:
            if self._client:
                try: self._client.close()
                except Exception: pass
                self._client = None
            self._channel = None

        try:
            client = paramiko.SSHClient()
            client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            client.connect(host, port=port, username=username, password=password,
                           timeout=5, allow_agent=False, look_for_keys=False)
            channel = client.invoke_shell(term='xterm-256color', width=120, height=40)
            channel.settimeout(0.2)

            with self._lock:
                self._client = client
                self._channel = channel

            self.term = TermBuffer(120, 40)
            self._running = True
            self.connection_changed.emit(True)
            self._reader_loop()
        except Exception as e:
            self.error_occurred.emit(f"SSH 连接失败: {e}")

    def disconnect(self):
        """ 断开连接。"""
        self._running = False
        with self._lock:
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
        """ 发送按键到 PTY。"""
        with self._lock:
            ch = self._channel
        if ch and not ch.closed:
            try:
                ch.send(data)
            except Exception as e:
                self.error_occurred.emit(f"发送失败: {e}")

    def _reader_loop(self):
        """ 循环读 PTY 输出 → term buffer → signal 通知 GUI。"""
        import time as _time
        while self._running:
            with self._lock:
                ch = self._channel
            if not ch or ch.closed:
                break

            try:
                if ch.recv_ready():
                    data = ch.recv(4096)
                    if data:
                        text = data.decode('utf-8', errors='replace')
                        self.term.write(text)
                        self.output_updated.emit()
                    else:
                        break
                else:
                    _time.sleep(0.03)
            except paramiko.buffered_pipe.PipeTimeout:
                continue
            except Exception as e:
                self.error_occurred.emit(f"读取错误: {type(e).__name__}: {e}")
                break

        self._running = False
        self.connection_changed.emit(False)


# ── GUI ───────────────────────────────────────────────────────────

class TerminalTab(QWidget):
    """ SSH 终端 —— invoke_shell + 内嵌 TermBuffer，体验接近 Xshell。"""

    def __init__(self):
        super().__init__()
        self._worker = _SshWorker()
        self._connected = False

        self._setup_ui()
        self._connect_signals()

    def _setup_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(4, 4, 4, 4)

        # 连接栏
        bar = QHBoxLayout()
        bar.addWidget(QLabel("Host:"))
        self.host = QLineEdit("192.168.3.171"); self.host.setFixedWidth(130); bar.addWidget(self.host)
        bar.addWidget(QLabel("User:"))
        self.user = QLineEdit("orangepi"); self.user.setFixedWidth(80); bar.addWidget(self.user)
        bar.addWidget(QLabel("Pwd:"))
        self.pwd = QLineEdit("orangepi")
        self.pwd.setEchoMode(QLineEdit.Password); self.pwd.setFixedWidth(80); bar.addWidget(self.pwd)

        self.connect_btn = QPushButton("Connect"); self.connect_btn.clicked.connect(self._on_connect)
        bar.addWidget(self.connect_btn)
        self.disconnect_btn = QPushButton("Disconnect"); self.disconnect_btn.clicked.connect(self._on_disconnect)
        self.disconnect_btn.setEnabled(False); bar.addWidget(self.disconnect_btn)
        bar.addStretch()
        self.status_lbl = QLabel("⚫"); bar.addWidget(self.status_lbl)
        layout.addLayout(bar)

        # 终端显示
        self.term_view = QPlainTextEdit()
        self.term_view.setReadOnly(True)
        self.term_view.setFont(QFont("Courier New", 11))
        self.term_view.setStyleSheet("""
            QPlainTextEdit {
                background-color: #1e1e1e;
                color: #d4d4d4;
                border: 1px solid #3c3c3c;
                selection-background-color: #264f78;
            }
        """)
        self.term_view.setCursorWidth(8)
        self.term_view.installEventFilter(self)
        layout.addWidget(self.term_view)

    def _connect_signals(self):
        self._worker.output_updated.connect(self._refresh)
        self._worker.connection_changed.connect(self._on_connection)
        self._worker.error_occurred.connect(self._on_error)

    # ── 按键转发 ─────────────────────────────────────────────────

    def eventFilter(self, obj, event):
        if obj is not self.term_view or not self._connected:
            return super().eventFilter(obj, event)
        if event.type() == event.Type.KeyPress:
            self._handle_key(event)
            return True
        return super().eventFilter(obj, event)

    def _handle_key(self, event):
        key = event.key()
        mod = event.modifiers()

        if mod == event.modifier().ControlModifier & ~event.modifier().ShiftModifier:
            ctrl_map = {
                event.Key_C: b'\x03', event.Key_D: b'\x04', event.Key_Z: b'\x1a',
                event.Key_L: b'\x0c', event.Key_A: b'\x01', event.Key_E: b'\x05',
                event.Key_U: b'\x15', event.Key_W: b'\x17', event.Key_K: b'\x0b',
            }
            if key in ctrl_map:
                self._worker.send(ctrl_map[key])
                return

        key_map = {
            event.Key_Return: b'\r', event.Key_Enter: b'\r',
            event.Key_Backspace: b'\x7f',
            event.Key_Tab: b'\t', event.Key_Escape: b'\x1b',
            event.Key_Up: b'\x1b[A', event.Key_Down: b'\x1b[B',
            event.Key_Right: b'\x1b[C', event.Key_Left: b'\x1b[D',
            event.Key_Home: b'\x1b[H', event.Key_End: b'\x1b[F',
            event.Key_Delete: b'\x1b[3~',
            event.Key_PageUp: b'\x1b[5~', event.Key_PageDown: b'\x1b[6~',
        }
        if key in key_map:
            self._worker.send(key_map[key])
            return

        text = event.text()
        if text:
            self._worker.send(text.encode('utf-8'))

    # ── 槽 ───────────────────────────────────────────────────────

    def _refresh(self):
        """ 用 TermBuffer 的最新内容刷新 QPlainTextEdit。"""
        self.term_view.setPlainText(str(self._worker.term))
        sb = self.term_view.verticalScrollBar()
        sb.setValue(sb.maximum())

    def _on_connection(self, connected: bool):
        self._connected = connected
        if connected:
            self.status_lbl.setText("●"); self.status_lbl.setStyleSheet("color: #27ae60; font-weight: bold;")
            self.connect_btn.setEnabled(False); self.disconnect_btn.setEnabled(True)
            self.term_view.setFocus()
        else:
            self.status_lbl.setText("⚫"); self.status_lbl.setStyleSheet("color: #95a5a6;")
            self.connect_btn.setEnabled(True); self.disconnect_btn.setEnabled(False)

    def _on_error(self, msg: str):
        self.term_view.setPlainText(str(self._worker.term) + f"\n[ERROR] {msg}")
        self.connect_btn.setEnabled(True)

    def _on_connect(self):
        host = self.host.text().strip(); user = self.user.text().strip(); pwd = self.pwd.text().strip()
        self.term_view.setPlainText(f"Connecting to {user}@{host} ...")
        self.connect_btn.setEnabled(False)
        threading.Thread(target=self._worker.connect, args=(host, 22, user, pwd), daemon=True).start()

    def _on_disconnect(self):
        self._worker.disconnect()
