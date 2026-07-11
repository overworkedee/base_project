#!/usr/bin/env python3
""" PC Dashboard —— Orange Pi 5 Plus 状态监控上位机 """

import sys
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
