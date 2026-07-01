# 硬测 Demo 设计

## 概述

在 `hw/` 下创建一个统一的命令行交互式硬件测试程序，替换现有 `demo_main.c`。用户输入命令执行不同硬件模块的测试，带 `help` 命令查看用法。

## 构建控制

有 `HW_BUILD_DEMO` CMake option（默认 OFF，现有模式保持）：

```bash
cmake -DHW_BUILD_DEMO=ON ..
make -j$(nproc)
./hw/hw_demo
```

## 交互模型

- 提示符 `hw> `，循环读输入
- `quit` 退出
- 每行一个命令，空格分隔 token

## 命令列表（初版）

| 命令 | 行为 | 备注 |
|------|------|------|
| `help` | 打印所有命令及格式 | 硬编码帮助文本 |
| `led on` | `led_open("blue_led")` → `led_on()` | 每次 open/close |
| `led off` | `led_open("blue_led")` → `led_off()` | 每次 open/close |
| `led heartbeat` | `led_open("blue_led")` → `led_set_trigger(HEARTBEAT)` | 每次 open/close |
| `led none` | `led_open("blue_led")` → `led_set_trigger(NONE)` | 每次 open/close |
| `i2c scan` | 遍历 `/dev/i2c-0` ~ `/dev/i2c-6`，探测 0x03-0x77 设备 | 每个地址一次 read 探测 |
| `quit` | 退出程序 | |

## 实现

- 单文件 `hw/demo/demo_main.c`，替换现有文件
- `main` 循环：`printf("hw> ")` → `fgets` → `strtok` 拆 token → `strcmp` 匹配 → handler 函数
- LED handler 每次 open/close（简化逻辑，避免句柄管理）
- 错误输入输出 `Unknown command: '%s', type 'help' for usage`
- `help` 函数硬编码输出所有命令格式

## 错误处理

- LED 命令：`led_open` 失败打印错误并返回
- I2C 命令：bus 不可用时打印 `not available`
- 未知命令：提示 `help`

## 测试

无独立测试文件。在板上手动运行验证。
