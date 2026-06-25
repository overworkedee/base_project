# HW IO Framework Design

## Overview

基于 Linux 内核接口的硬件 IO 通信框架，以静态库（`libhw.a`）形式编译，面向 Orange Pi 5 Plus (RK3588) 平台。采用**两层分离架构**：总线驱动层 + 设备驱动层，支持 GPIO、ADC、I2C、CAN 等，设备层后续按模板扩展。

## Architecture

### 分层设计

```
应用层 (demo_main.c / 项目代码)
  │
  ├── 设备驱动层 (dev/)       ← 持有总线句柄,处理IC协议
  │     ├── dev_template.h    ← 设备抽象模板
  │     └── ...               ← 具体 IC 驱动（后续添加）
  │
  ├── 总线驱动层 (bus/)       ← 封装 Linux 内核接口
  │     ├── bus_i2c.c/h
  │     ├── bus_can.c/h
  │     ├── bus_gpio.c/h
  │     └── bus_adc.c/h
  │
  └── OS 层 (/dev/xxx, pthread)
```

- **总线层**：封装 `open/read/write/ioctl` 等 Linux 系统调用，对外暴露不透明句柄和传输接口
- **设备层**：通过依赖注入方式持有总线句柄，不直接操作文件描述符
- **资源互斥**：总线句柄内部持有 `pthread_mutex_t`，同一总线句柄传递给多个设备，自动共享互斥

### Design Pattern: Bus Handle + Device Context

```
bus_i2c_t* bus = bus_i2c_open("/dev/i2c-1");      // 打开总线
dev_sensor_t* s = dev_sensor_init(s, bus, 0x77);   // 设备挂载到总线上
dev_sensor_read(s, buff, len);                      // 设备操作 → 内部调用 bus_i2c_transfer()
```

- 总线句柄不透明（前向声明 + 内部实现）
- 设备 `init` 时注入总线句柄，不自己打开总线
- 同一条总线句柄可以传给多个设备，互斥锁自然共享

## Directory Structure

```
hw/
├── CMakeLists.txt              # 编译为静态库 libhw.a
├── include/
│   └── hw/
│       ├── bus/
│       │   ├── bus_i2c.h       # I2C 总线封装
│       │   ├── bus_can.h       # CAN 总线封装
│       │   ├── bus_gpio.h      # GPIO 控制
│       │   └── bus_adc.h       # ADC 控制
│       ├── dev/
│       │   ├── dev_template.h  # 设备抽象模板（结构体 + 接口规范）
│       │   └── ...             # 未来具体 IC 的头文件
│       ├── hw_types.h          # 公共类型（句柄类型、返回值）
│       └── hw_error.h          # 错误码枚举
├── src/
│   ├── bus/
│   │   ├── bus_i2c.c
│   │   ├── bus_can.c
│   │   ├── bus_gpio.c
│   │   └── bus_adc.c
│   ├── dev/
│   │   └── ...                 # 未来具体 IC 的实现
│   ├── hw_error.c              # 错误码转字符串
│   └── hw_mutex.c              # 平台互斥锁封装（pthread）
├── demo/
│   └── demo_main.c             # 编译选项控制（CMake option HW_BUILD_DEMO）
└── tests/
    ├── test_bus_i2c.c
    ├── test_bus_gpio.c
    └── ...
```

## Bus Layer Interface

每种总线暴露相同的模式：

- `open` — 打开设备节点，初始化句柄和互斥锁
- `transfer` / `read` / `write` — 核心传输操作，内部自动加锁
- `close` — 释放资源、销毁锁

### I2C Bus Example

```c
typedef struct bus_i2c_ctx bus_i2c_t;

bus_i2c_t* bus_i2c_open(const char* device, uint8_t controller_addr);
int        bus_i2c_transfer(bus_i2c_t* bus, uint8_t* tx, size_t tx_len,
                            uint8_t* rx, size_t rx_len);
void       bus_i2c_close(bus_i2c_t* bus);
```

### Internal Structure (opaque)

```c
struct bus_i2c_ctx {
    int              fd;
    pthread_mutex_t  lock;
};
```

## Device Layer Template

设备不打开总线，只持有句柄。新增 IC 的设备驱动遵循以下模式：

```c
typedef struct {
    bus_i2c_t* bus;        // 依赖注入的总线句柄
    uint8_t    addr;       // 设备地址
    // IC 内部状态（寄存器缓存、配置等）
} dev_template_t;

hw_err_t dev_template_init(dev_template_t* dev, bus_i2c_t* bus, uint8_t addr);
hw_err_t dev_template_read_xxx(dev_template_t* dev, void* data, size_t len);
hw_err_t dev_template_write_xxx(dev_template_t* dev, const void* data, size_t len);
```

## Error Handling

```c
typedef enum {
    HW_OK                = 0,
    HW_ERR_BUS_OPEN,          // 总线打开失败
    HW_ERR_BUS_TRANSFER,      // 传输失败
    HW_ERR_DEV_ADDR,          // 设备地址无效
    HW_ERR_DEV_NOT_FOUND,     // 设备无响应
    HW_ERR_MUTEX_INIT,        // 锁初始化失败
    HW_ERR_PARAM,             // 参数非法
} hw_err_t;

const char* hw_err_str(hw_err_t err);
```

所有 API 返回 `hw_err_t`，`HW_OK` = 0，与 C 惯用法兼容。

## Build Integration

- 编译为静态库 `libhw.a`
- 通过 CMake `option(HW_BUILD_DEMO OFF)` 控制 `demo_main.c` 是否编译
- 项目 `CMakeLists.txt` 中 `add_subdirectory(hw)` 引入
- 外部使用时 `target_link_libraries(hello_world hw)`

## Extension Guide

### 新增一种总线（如 SPI）

1. 在 `include/hw/bus/` 创建 `bus_spi.h`
2. 在 `src/bus/` 创建 `bus_spi.c`
3. 在 `CMakeLists.txt` 中追加源文件
4. 已有代码无需修改

### 新增一颗 I2C 设备

1. 在 `include/hw/dev/` 创建 `dev_<chip>.h`
2. 在 `src/dev/` 创建 `dev_<chip>.c`（可选，只有复杂 IC 需要）
3. 按 `dev_template.h` 的接口规范实现 `init/read/write`

## Implementation Notes

- 第一期原型：I2C 总线（`bus_i2c.c/h`） + 设备模板（`dev_template.h`） + 公共基础设施（错误码、互斥锁、类型定义）
- 后续按需添加 CAN、GPIO、ADC 总线驱动
- GPIO、ADC 的总线句柄模式与 I2C 一致：`bus_gpio_open(pin)` → `bus_gpio_read/set()` → `bus_gpio_close()`
