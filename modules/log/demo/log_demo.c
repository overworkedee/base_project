/**
 * log_demo.c — 日志模块使用示例
 *
 * 演示四种日志等级的使用方式和输出格式。
 */
#include "log/log.h"

#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("=== Log Module Demo ===\n\n");

    /* 初始化：DEBUG 等级，输出全部日志 */
    hw_err_t ret = log_init("/tmp/project.log", LOG_DEBUG);
    if (ret != HW_OK) {
        printf("log_init failed: %d\n", ret);
        return 1;
    }

    printf("Log module initialized.\n");
    printf("Log file: /tmp/project.log\n");
    printf("Min level: LOG_DEBUG\n\n");

    /* 四级日志演示 */
    LOG_DEBUG("这是一条 DEBUG 日志，变量 x=%d", 42);
    LOG_INFO("这是一条 INFO 日志，I2C 总线初始化成功");
    LOG_WARN("这是一条 WARN 日志，重试次数=%d", 3);
    LOG_ERROR("这是一条 ERROR 日志，设备无响应 addr=0x%02x", 0x77);

    /* 运行时切换等级 */
    printf("\n--- Switching to LOG_WARN ---\n\n");
    log_set_level(LOG_WARN);
    LOG_DEBUG("这条 DEBUG 不会被看到");
    LOG_INFO("这条 INFO 也不会被看到");
    LOG_WARN("这条 WARN 会被看到");
    LOG_ERROR("这条 ERROR 会被看到");

    /* 切回 DEBUG */
    printf("\n--- Switching back to LOG_DEBUG ---\n\n");
    log_set_level(LOG_DEBUG);
    LOG_DEBUG("DEBUG 日志恢复输出");

    /* 模拟一次带有多信息的日志 */
    LOG_INFO("传感器读取: addr=0x%02x, reg=0x%02x, value=%d", 0x42, 0x10, 128);

    printf("\n=== Demo Complete ===\n");
    printf("Check /tmp/project.log for output\n");

    log_deinit();
    return 0;
}
