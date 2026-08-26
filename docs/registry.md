# 应用层注册机制指南（app_cmd_svc 能力表）

## 1. 为什么需要它——旧的耦合问题

重构前，应用层存在三类耦合：

| 耦合点 | 问题 |
|--------|------|
| main.c 逐一注册 handler | `app_cmd_register(g_cmd, CMD_LED, cmd_handler_led, g_led)` ✕4，main 感知所有 handler 细节，新增命令必须改 main |
| app 模块持有 cmd 内部类型 | `cmd_subscription_mgr_t*` 直接下发给 app_sensor / app_system，模块被绑定到 cmd 的实现结构 |
| 日志推送耦合 | `on_log_push` 直接调 `cmd_subscription_push(sub_mgr, ...)`，main 需要了解订阅管理器的内部 API |

重构后：**cmd 模块对外只暴露一张"能力表"（函数指针集合），各 app 模块通过回调完成注册与推送**，模块之间只依赖抽象签名（`app_registry.h`），不依赖任何内部类型。

## 2. 核心概念：app_cmd_svc_t 能力表

定义在 `user/app_registry.h`：

```c
typedef struct {
    void*                owner;            /* 能力实现实例（app_cmd_t*） */
    app_cmd_register_fn  register_cmd;     /* 注册命令 handler          */
    app_publish_fn       publish_data;     /* 向订阅者推送数据流        */
    app_subscribe_fn     subscribe_data;   /* 添加订阅                  */
    app_unsubscribe_fn   unsubscribe_data; /* 移除订阅                  */
} app_cmd_svc_t;
```

- **owner**：回调实现者的实例指针（这里是 `app_cmd_t*`），回调第一个参数
- **每个字段**：一个函数指针 + 固定签名，由 cmd 模块在 `app_cmd_create()` 内填充实现
- **获取方式**：`const app_cmd_svc_t* svc = app_cmd_get_svc(g_cmd);`

回调签名一览（全部在 `app_registry.h`）：

| 回调 | 签名 | 能力 |
|------|------|------|
| `register_cmd` | `int (*)(void* owner, uint8_t cmd, cmd_handler_fn handler, void* ctx)` | 把 (cmd → handler) 登记进 dispatcher |
| `publish_data` | `int (*)(void* owner, uint8_t cmd, uint16_t data_id, const uint8_t* value, size_t len)` | 向订阅 data_id 的所有连接推送数据帧 |
| `subscribe_data` | `int (*)(void* owner, uint16_t data_id, uint32_t interval_ms, cmd_conn_t* conn)` | 注册订阅（间隔 0 = 事件驱动） |
| `unsubscribe_data` | `int (*)(void* owner, uint16_t data_id, cmd_conn_t* conn)` | 取消订阅 |

## 3. 注册时序：谁在什么时候注册

```
main()
  │
  ├── g_cmd = app_cmd_create()
  │        └── 填充 cmd->svc = {owner, register_cmd, publish_data, ...}
  │
  ├── svc = app_cmd_get_svc(g_cmd)          ← 取能力表（只读共享）
  │
  ├── app_led_create(g_led, svc)
  │        └── svc->register_cmd(owner, CMD_LED, cmd_handler_led, app)
  │              └── cmd_dispatcher_register(CMD_LED, handler, app)
  ├── app_sensor_create(g_sht30, svc)
  │        └── svc->register_cmd(owner, CMD_SENSOR, cmd_handler_sensor, app)
  ├── app_system_create(svc)
  │        └── svc->register_cmd(owner, CMD_SYSTEM, cmd_handler_system, app)
  └── app_camera_create(svc)
           └── svc->register_cmd(owner, CMD_CAMERA, cmd_handler_camera, app)
```

**关键点**：
- **自注册**：每个 app 模块在 `app_<feature>_create()` 内部调用 `svc->register_cmd`，handler 声明为 `static`，不对外暴露
- **main 零感知**：main.c 只 `create`，不 include 任何 handler，不加任何注册语句
- **生命周期同步**：svc 的 owner 是 `g_cmd`，因此**必须保证 app 模块销毁先于 app_cmd_destroy**（见 §6 清理顺序）

## 4. 数据推送：采集线程怎么把数据发给订阅者

```
sensor_thread (app_sensor.c 内部)
  │
  └── svc->publish_data(owner, CMD_SENSOR, CMD_DATA_TEMPERATURE, val_be, 4)
        └── cmd_subscription_push(sub_mgr, CMD_SENSOR, ...)
              └── 遍历订阅表，对每个 conn 组帧发送
```

app_sensor 只持有 `const app_cmd_svc_t* svc`，**不知道 sub_mgr 长什么样**。
同样，日志推送（main.c 的 `on_log_push`）也改为：

```c
static void on_log_push(uint8_t level, const char* msg, void* ctx)
{
    const app_cmd_svc_t* svc = (const app_cmd_svc_t*)ctx;
    /* 构造 [level, reserved, ts, msg] 数据 */
    svc->publish_data(svc->owner, CMD_SYSTEM, CMD_DATA_LOG, buf, len);
}
```

注册方式：`log_set_subscribe_callback(on_log_push, (void*)svc)` —— ctx 就是能力表指针。

## 5. 扩展新命令（motor 示例，3 步）

```c
/* 1. cmd_frame.h 加命令号 */
#define CMD_MOTOR  0x04

/* 2. 新文件 user/app_motor.c/.h（加入顶层 CMakeLists 的 add_executable） */
app_motor_t* app_motor_create(motor_t* motor, const app_cmd_svc_t* svc)
{
    app_motor_t* app = calloc(1, sizeof(*app));
    if (!app) return NULL;
    app->motor = motor;
    svc->register_cmd(svc->owner, CMD_MOTOR, cmd_handler_motor, app); /* 自注册 */
    return app;
}

/* 3. main.c 组装（一行） */
g_motor = app_motor_create(g_motor_hw, app_cmd_get_svc(g_cmd));
```

## 6. 生命周期与清理顺序（重点）

能力表 svc 的 owner（`g_cmd`）生命周期覆盖所有 app 模块：

```
cleanup()  atexit 触发
  │
  ├── app_sensor_stop/destroy      ← 采集线程持有 svc，必须先退出线程
  ├── app_camera_stop/destroy      ← RTSP 子进程持有 self_path
  ├── app_led_destroy
  ├── app_system_destroy
  │
  ├── log_set_subscribe_callback(NULL, NULL)  ← 阻止 on_log_push 访问已死的 svc
  ├── app_cmd_destroy             ← 释放 sub_mgr / dispatcher / server
  ├── led_close / sht30_close     ← 可能调用 LOG_*
  └── log_deinit
```

**铁律**：`app_*_destroy` 全部执行完，才允许 `app_cmd_destroy`。

## 7. 目录与文件

```
user/app_registry.h    能力表定义（owner + 4 个回调签名）——所有 app 模块唯一依赖
user/app_cmd.c/.h      实现能力表 + 生命周期 + 监听 + 事件循环
user/app_led.c/.h      自注册 CMD_LED，handler static
user/app_sensor.c/.h   自注册 CMD_SENSOR，采集线程经 svc->publish_data 推送
user/app_system.c/.h   自注册 CMD_SYSTEM，日志订阅走 svc->subscribe_data
user/app_camera.c/.h   自注册 CMD_CAMERA，内部管理 RTSP 子进程
user/main.c            组装根：create 全部模块 → run → atexit cleanup
```

## 8. 设计取舍

| 决策 | 原因 |
|------|------|
| 能力表用函数指针而非直接传 sub_mgr | 模块依赖接口签名，不依赖实现结构；可独立测试、可替换实现 |
| 回调都带 `void* owner` | 一份能力表即可支持多个实例（未来多 cmd server 场景） |
| 各模块自注册 handler | main 零注册语句，新增命令不动 main；handler 可保持 static 不外泄 |
| svc 只读共享 + owner 生命周期约束 | 避免重复创建/悬挂指针；代价是销毁顺序有强约束（§6） |

## 9. 后续可扩展方向

- 增加 `register_event` 回调：让模块向 app_cmd 注册周期性定时事件（替代各自开线程）
- 能力表细分：`app_sensor_svc_t`（采集数据提供）+ `app_stream_svc_t`（推流控制），按需注入
- 统一模块结构约定：`create(hw_deps, svc)` + `start/stop/destroy` 的模板宏