# LED 控制模块设计

## 概述

为 Orange Pi 5 Plus 板载 LED（blue_led、green_led）提供 sysfs 控制接口，支持开关和 trigger 切换。

## 背景

- 目标平台：Orange Pi 5 Plus（RK3588）
- 对应设备树：`rk3588-orangepi-5-plus.dts`
- LED 设备：`blue_led`（GPIO3_PA6，默认 heartbeat）、`green_led`（GPIO3_PB1，默认无 trigger）
- LED 已在内核中注册为 gpio-leds，通过 `/sys/class/leds/<name>/` 暴露控制接口

## 架构

```
hw/
├── include/hw/dev/dev_led.h    ← 公开头文件
├── src/dev/dev_led.c            ← sysfs 实现
├── CMakeLists.txt                ← 新增源文件和测试目标
└── tests/test_dev_led.c          ← 单元测试
```

遵循现有 hw/ 框架模式：不透明句柄 + 依赖注入 + 内部互斥锁。

## API

### 类型定义

```c
typedef enum {
    LED_TRIGGER_NONE      = 0,
    LED_TRIGGER_HEARTBEAT = 1,
} led_trigger_t;

typedef struct led_ctx led_t;  // 不透明句柄
```

### 生命周期

```c
led_t*   led_open(const char* name);   // @param name LED label，如 "blue_led"
void     led_close(led_t* led);        // 安全处理 NULL
```

### 开关控制

```c
hw_err_t led_on(led_t* led);   // 写入 max_brightness
hw_err_t led_off(led_t* led);  // 写入 0
```

### Trigger 切换

```c
hw_err_t led_set_trigger(led_t* led, led_trigger_t trigger);
```

### 状态读取

```c
hw_err_t led_get_brightness(led_t* led, int* brightness);
hw_err_t led_get_trigger(led_t* led, led_trigger_t* trigger);
```

## 内部实现

### sysfs 路径

- `/sys/class/leds/<name>/brightness` — 读写亮度值
- `/sys/class/leds/<name>/trigger` — 读当前 trigger、写目标 trigger
- `/sys/class/leds/<name>/max_brightness` — 读取最大亮度

### 关键逻辑

| 操作 | 实现 |
|------|------|
| `led_on` | 写入 `max_brightness` 到 brightness 文件 |
| `led_off` | 写入 `"0"` 到 brightness 文件 |
| `led_set_trigger` | hearteat → `"heartbeat"`，none → `"none"` |
| `led_get_brightness` | 读取 brightness 文件，解析为 int |
| `led_get_trigger` | 读取 trigger 文件，解析当前选中的 trigger（`[...]` 包裹项） |

### 线程安全

参照 `bus_i2c`，使用 `hw_mutex_t` 保护所有 sysfs 文件操作。

## 错误码

在 `hw_error.h` 中新增：

```c
HW_ERR_IO,  /* 通用文件/IO 操作失败 */
```

## 构建

- 源文件 `src/dev/dev_led.c` 加入 `CMakeLists.txt` 的 `HW_SOURCES`
- 新增测试目标 `test_dev_led`，链接 `hw` 库

## 测试

需创建 `tests/test_dev_led.c`，测试用例需要 mock sysfs 路径或在实际板上运行。测试覆盖：
1. `led_open` 参数校验（NULL 返回 NULL）
2. `led_on` / `led_off` 功能验证
3. `led_set_trigger` 两种 trigger 切换
4. `led_get_brightness` / `led_get_trigger` 状态读取
5. `led_close` NULL 安全
