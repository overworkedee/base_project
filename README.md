# Orange Pi 5 Plus (RK3588) 嵌入式C项目

## 编译

```bash
# 正式版本编译（交叉编译 ARM64，不包含测试和 demo）
./build.sh

# 带调试编译
./build.sh -DCMAKE_BUILD_TYPE=Debug

# 开发阶段：带测试 + 硬测 demo
BUILD_TESTS=1 HW_BUILD_DEMO=1 ./build.sh

# 切换工具链
export TOOLCHAIN_PATH=/path/to/other/toolchain
./build.sh
```

输出目录：`out/bin/`

| 文件 | 说明 |
|------|------|
| `out/bin/project_app` | 主程序 |

## 环境变量

`build.sh` 自动 `source env/rk3588_product_orangerpi5plus.env`。所有变量支持命令行覆盖（`:-` 语法）。

| 变量 | 默认 | 说明 |
|------|------|------|
| `HW_BUILD_DEMO` | `0` | 1=编译硬测 CLI demo（`hw_demo`） |
| `LOG_BUILD_DEMO` | `0` | 1=编译日志 demo（`log_demo`） |
| `LOG_ENABLE_DEBUG` | `1` | 1=编译时保留 `LOG_DEBUG`，0=裁剪 |
| `BUILD_TESTS` | `0` | 1=编译单元测试（test_*），0=不编译 |
| `TOOLCHAIN_PATH` | `$HOME/orangepi-build/toolchains/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu` | 交叉编译工具链路径 |
| `CROSS_COMPILE` | `aarch64-none-linux-gnu-` | 交叉编译器前缀 |
| `ARCH` | `arm64` | 目标架构 |
| `PLATFORM` | `rk3588` | 目标平台 |

默认配置用于正式版本：关闭所有 demo 和测试，仅生成干净的主程序。开发时按需通过命令行开启：

```bash
BUILD_TESTS=1 HW_BUILD_DEMO=1 LOG_BUILD_DEMO=1 ./build.sh
```

## 项目结构

```
project/
├── user/                          # 应用源码
│   └── main.c                     # 入口
├── hw/                            # 硬件 IO 框架（libhw.a）
│   ├── include/hw/
│   │   ├── hw_error.h             # 错误码定义
│   │   ├── hw_mutex.h             # 互斥锁封装
│   │   ├── hw_types.h             # 公共类型
│   │   ├── bus/bus_i2c.h          # I2C 总线驱动
│   │   └── dev/
│   │       ├── dev_template.h     # I2C 设备驱动模板
│   │       └── dev_led.h          # LED sysfs 控制
│   ├── src/
│   │   ├── hw_error.c、hw_mutex.c
│   │   ├── bus/bus_i2c.c
│   │   └── dev/dev_led.c
│   ├── demo/demo_main.c           # 硬测 CLI demo
│   └── tests/
│       ├── test_bus_i2c.c
│       └── test_dev_led.c
├── modules/                       # 通用功能模块
│   └── log/                       # 日志模块（liblog.a）
│       ├── include/log/log.h
│       ├── src/log.c
│       ├── tests/test_log.c
│       └── demo/log_demo.c
├── part/                          # 第三方 .so/.a 库
├── env/                           # 平台环境配置
│   └── rk3588_product_orangerpi5plus.env
├── out/bin/                       # 编译产物输出
├── build.sh                       # 构建脚本
└── CMakeLists.txt                 # 顶层 CMake
```

## 模块使用

### LED 控制

```c
#include "hw/dev/dev_led.h"

led_t* led = led_open("blue_led");
led_on(led);                         // 开
led_set_trigger(led, LED_TRIGGER_HEARTBEAT);  // 心跳闪烁
led_close(led);
```

### I2C 总线

```c
#include "hw/bus/bus_i2c.h"

bus_i2c_t* bus = bus_i2c_open("/dev/i2c-1");
if (bus) {
    uint8_t data;
    bus_i2c_read(bus, 0x50, &data, 1);
    bus_i2c_close(bus);
}
```

### 日志模块

所有打印输出必须使用日志模块，**禁止直接使用 printf/fprintf**。

```c
#include "log/log.h"

log_init("/tmp/project.log", LOG_DEBUG);

LOG_DEBUG("变量 x=%d", 42);
LOG_INFO("I2C 总线初始化成功");
LOG_WARN("重试次数=%d", 3);
LOG_ERROR("设备无响应");

log_deinit();
```

链接：

```cmake
target_link_libraries(your_app hw log)
```

日志等级：`DEBUG(0)` < `INFO(1)` < `WARN(2)` < `ERROR(3)`，越小越详细。

### 硬测 Demo

在开发板上交互式测试硬件：

```bash
# 编译
HW_BUILD_DEMO=1 ./build.sh

# 上传并运行
scp out/bin/hw_demo root@<IP>:~/
ssh root@<IP>
./hw_demo

hw> led on          # 打开 blue_led
hw> led heartbeat   # 心跳闪烁
hw> i2c scan        # 扫描 I2C 总线
hw> help            # 查看所有命令
hw> quit
```

## 编码规范

所有函数使用中文注释，统一格式：

```c
/**
 * 简短描述函数的功能
 *
 * @param param1  参数含义和约束
 * @param param2  参数含义和约束
 * @return        返回值含义
 * @note          特殊注意事项
 */
hw_err_t some_function(int param1, void* param2);
```

## 部署到开发板

```bash
# 编译正式版本
./build.sh

# 拷贝到板子
scp out/bin/project_app root@<板子IP>:~/

# SSH 登录运行
ssh root@<板子IP>
./project_app
```
