# Orange Pi 5 Plus (RK3588) 嵌入式C项目

## 编译

```bash
# 完整编译（交叉编译 ARM64）
./build.sh

# 带自定义 CMake 参数
./build.sh -DCMAKE_BUILD_TYPE=Debug

# 切换工具链
export TOOLCHAIN_PATH=/path/to/other/toolchain
./build.sh
```

## CMake Options

| Option | 默认值 | 说明 |
|--------|--------|------|
| `HW_BUILD_DEMO` | `OFF` | 编译 I2C 硬件扫描 demo（`hw_demo`） |
| `LOG_ENABLE_DEBUG` | `1` | 启用 DEBUG 级别日志（通过 env 文件控制） |

用法：

```bash
# 编译 I2C 扫描 demo
./build.sh -DHW_BUILD_DEMO=ON
```

## 环境变量（env 文件）

构建前 `build.sh` 会自动 `source env/rk3588_product_orangerpi5plus.env`，定义以下关键变量：

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `TOOLCHAIN_PATH` | `$HOME/orangepi-build/toolchains/gcc-arm-11.2-2022.02-x86_64-aarch64-none-linux-gnu` | 交叉编译工具链路径 |
| `CROSS_COMPILE` | `aarch64-none-linux-gnu-` | 交叉编译器前缀 |
| `LOG_ENABLE_DEBUG` | `1` | 1=启用 DEBUG 日志，0=编译期裁剪 |
| `LOG_BUILD_DEMO` | `1` | 1=编译日志 demo（log_demo），0=不编译 |
| `ARCH` | `arm64` | 目标架构 |
| `PLATFORM` | `rk3588` | 目标平台 |

## 项目结构

```
project/
├── user/                          # 应用源码
│   └── main.c                     # 入口
├── hw/                            # 硬件 IO 框架（libhw.a）
│   ├── include/hw/
│   │   ├── hw_error.h             # 错误码
│   │   ├── hw_mutex.h             # 互斥锁封装
│   │   ├── bus/bus_i2c.h          # I2C 总线驱动
│   │   └── dev/dev_template.h     # 设备驱动模板
│   ├── src/
│   │   ├── hw_error.c、hw_mutex.c
│   │   └── bus/bus_i2c.c
│   ├── demo/demo_main.c           # I2C 扫描示例
│   └── tests/test_bus_i2c.c
├── modules/                       # 通用功能模块
│   └── log/                       # 日志模块（liblog.a）
│       ├── include/log/log.h
│       ├── src/log.c
│       ├── tests/test_log.c
│       └── demo/log_demo.c
├── part/                          # 第三方 .so/.a 库
├── env/                           # 平台环境配置
│   └── rk3588_product_orangerpi5plus.env
├── build.sh                       # 构建脚本
└── CMakeLists.txt                 # 顶层 CMake
```

## 模块使用

### I2C 硬件框架（hw）

```c
#include "hw/bus/bus_i2c.h"
#include "hw/dev/dev_template.h"

int main(void) {
    /* 打开 I2C 总线 */
    bus_i2c_t* bus = bus_i2c_open("/dev/i2c-1");
    if (!bus) return -1;

    /* 扫描设备 */
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        uint8_t dummy;
        if (bus_i2c_read(bus, addr, &dummy, 1) == HW_OK) {
            printf("Device at 0x%02x\n", addr);
        }
    }

    bus_i2c_close(bus);
    return 0;
}
```

CMakeLists.txt 中链接：

```cmake
target_link_libraries(your_app hw)
```

### 日志模块（log）

```c
#include "log/log.h"

int main(void) {
    /* 初始化：DEBUG 等级，输出全部日志 */
    log_init("/tmp/project.log", LOG_DEBUG);

    /* 四级日志 */
    LOG_DEBUG("变量 x=%d", 42);
    LOG_INFO("I2C 总线初始化成功");
    LOG_WARN("重试次数=%d", 3);
    LOG_ERROR("设备无响应 addr=0x%02x", 0x77);

    /* 运行时切换等级（只输出 WARN 及以上） */
    log_set_level(LOG_WARN);

    /* 退出前关闭 */
    log_deinit();
    return 0;
}
```

CMakeLists.txt 中链接：

```cmake
target_link_libraries(your_app log)
```

## 日志输出格式

```
[2026-06-27 14:32:05.123] [INFO ] [main.c:42 main] I2C 总线初始化成功
[2026-06-27 14:32:05.456] [WARN ] [sensor.c:88 read_sensor] 重试次数=3
[2026-06-27 14:32:05.789] [ERROR] [i2c.c:15 transfer] 设备无响应 addr=0x42
```

格式：`[时间.ms] [等级] [文件名:行号 函数名] 消息`

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
# 编译 demo
./build.sh -DHW_BUILD_DEMO=ON

# 拷贝到板子
scp build/hw/hw_demo root@<板子IP>:~/
scp build/modules/log/log_demo root@<板子IP>:~/

# SSH 登录运行
ssh root@<板子IP>
./hw_demo
./log_demo
```
