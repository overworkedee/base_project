#!/bin/bash
# 命令模块集成测试脚本 — 在 Orange Pi 5 Plus 上运行
# 需要先启动 project_app，然后运行本脚本

SOCAT=$(which socat)
if [ -z "$SOCAT" ]; then
    echo "ERROR: socat not found. Install with: apt install socat"
    exit 1
fi

echo "=== Test 1: LED On (Unix Socket) ==="
echo -ne '\xA5\x5A\x00\x01\x01\x01\x01' | socat - UNIX-CONNECT:/tmp/cmd.sock 2>/dev/null | xxd | head -1

echo "=== Test 2: LED Off (Unix Socket) ==="
echo -ne '\xA5\x5A\x00\x01\x01\x01\x00' | socat - UNIX-CONNECT:/tmp/cmd.sock 2>/dev/null | xxd | head -1

echo "=== Test 3: Read Temperature (Unix Socket) ==="
echo -ne '\xA5\x5A\x00\x00\x02\x02' | socat - UNIX-CONNECT:/tmp/cmd.sock 2>/dev/null | xxd | head -1

echo "=== Test 4: Read LED Status (Unix Socket) ==="
echo -ne '\xA5\x5A\x00\x01\x01\x02\x01' | socat - UNIX-CONNECT:/tmp/cmd.sock 2>/dev/null | xxd | head -1

echo "=== Test 5: System Info (Unix Socket) ==="
echo -ne '\xA5\x5A\x00\x00\x03\x02' | socat - UNIX-CONNECT:/tmp/cmd.sock 2>/dev/null | xxd | head -1

echo "=== Test 6: Subscribe Temperature (TCP) ==="
echo -ne '\xA5\x5A\x00\x04\x02\x03\x00\x01\x00\x00\x03\xE8' | socat - TCP:localhost:9527 2>/dev/null | xxd | head -1

echo "=== Test 7: System Info (TCP) ==="
echo -ne '\xA5\x5A\x00\x00\x03\x02' | socat - TCP:localhost:9527 2>/dev/null | xxd | head -1

echo "=== Tests complete. Check /tmp/project.log for details ==="
