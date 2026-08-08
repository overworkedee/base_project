/**
 * 视觉识别模块 —— 纯 C 接口
 *
 * 封装 OpenCV 核心功能（图像加载/灰度化/边缘检测/保存），
 * 所有 C++ 类型均隐藏在 .cpp 实现中，调用方只需 C 编译器。
 *
 * 用法示例:
 *   vision_image_t* img = vision_load("/path/to/photo.jpg");
 *   if (!img) return -1;
 *   vision_to_gray(img);
 *   vision_canny(img, 50, 150);
 *   vision_save(img, "/path/to/output.jpg");
 *   vision_destroy(img);
 */
#ifndef VISION_H
#define VISION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 不透明图像句柄，内部封装 cv::Mat */
typedef struct vision_image vision_image_t;

/* ── 生命周期 ─────────────────────────────────────────────────── */

vision_image_t* vision_load(const char* path);
void            vision_destroy(vision_image_t* img);

/* ── 内存构造（摄像头 NV12 帧） ─────────────────────────────────── */

vision_image_t* vision_create_from_nv12(const void* data, int width, int height,
                                        int stride);

/* ── 图像信息 ─────────────────────────────────────────────────── */

int vision_width(const vision_image_t* img);
int vision_height(const vision_image_t* img);
int vision_channels(const vision_image_t* img);

/* ── 图像处理（原地修改） ────────────────────────────────────── */

int vision_to_gray(vision_image_t* img);
int vision_canny(vision_image_t* img, double low_thresh, double high_thresh);

/* ── I/O ──────────────────────────────────────────────────────── */

int vision_save(const vision_image_t* img, const char* path);

/* ── 图像统计 ─────────────────────────────────────────────────── */

int vision_min_max(const vision_image_t* img, double* min_val, double* max_val);
int vision_count_nonzero(const vision_image_t* img, int* count);

#ifdef __cplusplus
}
#endif

#endif /* VISION_H */
