#!/usr/bin/env python3
""" PC Dashboard —— Orange Pi 5 Plus 状态监控上位机 """

import sys
import os

# 确保项目根目录在 Python 路径中（支持直接 python3 pc_dashboard/main.py 启动）
_PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _PROJECT_ROOT not in sys.path:
    sys.path.insert(0, _PROJECT_ROOT)

from PySide6.QtWidgets import QApplication
from pc_dashboard.gui.main_window import MainWindow


def main():
    app = QApplication(sys.argv)
    app.setApplicationName("PC Dashboard")
    app.setStyle("Fusion")  # 跨平台一致外观

    window = MainWindow()
    window.show()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
