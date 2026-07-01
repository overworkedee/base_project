#ifndef HW_ERROR_H
#define HW_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HW_OK                = 0,
    HW_ERR_BUS_OPEN,          /* 总线打开失败 */
    HW_ERR_BUS_TRANSFER,      /* 传输失败 */
    HW_ERR_DEV_ADDR,          /* 设备地址无效 */
    HW_ERR_DEV_NOT_FOUND,     /* 设备无响应 */
    HW_ERR_MUTEX,             /* 互斥锁操作失败 */
    HW_ERR_PARAM,             /* 参数非法 */
    HW_ERR_IO,                /* 通用文件/IO 操作失败 */
} hw_err_t;

const char* hw_err_str(hw_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* HW_ERROR_H */
