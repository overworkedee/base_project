#ifndef DEV_CAMERA_H
#define DEV_CAMERA_H

#include <stddef.h>
#include <stdint.h>
#include "hw/hw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 类型定义 ─────────────────────────────────────────────────────── */

typedef struct camera_ctx camera_t;

/* ── 生命周期 ─────────────────────────────────────────────────────── */

camera_t* camera_open(const char* path);
void      camera_close(camera_t* camera);

/* ── 设备查找 ─────────────────────────────────────────────────────── */

hw_err_t  camera_find_by_card(const char* card_substr, char* path, size_t cap);

/* ── 格式设置 ─────────────────────────────────────────────────────── */

hw_err_t  camera_set_format(camera_t* camera, uint32_t width, uint32_t height,
                            uint32_t fourcc);

/* ── 采集控制 ─────────────────────────────────────────────────────── */

hw_err_t  camera_start(camera_t* camera);
hw_err_t  camera_grab(camera_t* camera, uint8_t* dst, size_t cap, size_t* size);
void      camera_stop(camera_t* camera);

/* ── 帧信息 ───────────────────────────────────────────────────────── */

size_t    camera_frame_size(camera_t* camera);
uint32_t  camera_width(camera_t* camera);
uint32_t  camera_height(camera_t* camera);
uint32_t  camera_stride(camera_t* camera);

#ifdef __cplusplus
}
#endif

#endif /* DEV_CAMERA_H */
