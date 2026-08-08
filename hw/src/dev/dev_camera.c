/**
 * @file dev_camera.c
 * @brief V4L2 摄像头采集驱动实现
 *
 * 基于 Linux V4L2 用户空间 API 封装单帧抓取（拍照场景）：
 *   - 支持 MPLANE 与单平面两种捕获类型（rkisp 为 MPLANE）
 *   - mmap 映射 + 4 个 buffer 环形队列
 *   - camera_grab 阻塞等待一帧并拷贝到调用者缓冲区
 *
 * 典型流程：
 *   camera_open → camera_set_format → camera_start
 *   → camera_grab × N → camera_stop → camera_close
 *
 * 注意：V4L2 设备同一时间只能有一个打开者（拍照与 RTSP 推流
 * 需使用不同节点，如 rkisp mainpath 与 selfpath 可并行）。
 */

#include "hw/dev/dev_camera.h"
#include "hw/hw_mutex.h"
#include "log/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <dirent.h>

#include <linux/videodev2.h>

/* ── 内部结构 ─────────────────────────────────────────────────────── */

#define CAMERA_MAX_BUFS  4   /* mmap 环形队列缓冲数 */

/* 单个 mmap buffer */
struct camera_buf {
    void*   mem;      /* mmap 映射地址 */
    size_t  length;   /* 映射长度 */
};

struct camera_ctx {
    int                fd;          /* 设备文件描述符（open 后有效） */
    int                type;        /* 捕获类型: V4L2_BUF_TYPE_VIDEO_CAPTURE(_MPLANE) */
    uint32_t           width;       /* 帧宽（像素） */
    uint32_t           height;      /* 帧高（像素） */
    uint32_t           fourcc;      /* 像素格式（V4L2_PIX_FMT_*） */
    uint32_t           stride;      /* 每行字节数（bytesperline） */
    size_t             frame_size;  /* 单帧字节数（跨 plane 总长） */
    struct camera_buf  bufs[CAMERA_MAX_BUFS];  /* mmap 缓冲 */
    int                nbufs;       /* 实际请求的缓冲数 */
    int                streaming;   /* 是否处于 STREAMON 状态 */
};

/* ── 内部辅助函数 ─────────────────────────────────────────────────── */

/**
 * 向 V4L2 设备发送 ioctl，EINTR 时自动重试。
 *
 * @param fd    设备文件描述符
 * @param req   请求码
 * @param arg   请求参数指针
 * @return      0 成功，-1 失败（errno 保留）
 */
static int camera_ioctl(int fd, unsigned long req, void* arg)
{
    int ret;
    do {
        ret = ioctl(fd, req, arg);
    } while (ret == -1 && errno == EINTR);
    return ret;
}

/**
 * 判断像素格式是否为 YUV 4:2:0 系（NV12/NV21/NM12 等）。
 *
 * 4:2:0 的帧大小为 width*height*3/2，4:2:2 为 width*height*2。
 *
 * @param fourcc  V4L2 像素格式
 * @return        1 是 4:2:0，0 否
 */
static int camera_is_420(uint32_t fourcc)
{
    switch (fourcc) {
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
        return 1;
    default:
        return 0;
    }
}

/* ── 公开 API ─────────────────────────────────────────────────────── */

/**
 * 打开 V4L2 摄像头设备。
 *
 * 校验设备支持 Video Capture 能力；MPLANE 与单平面自动识别。
 *
 * @param path  设备节点路径，如 "/dev/video11" 或 "/dev/video-camera0"
 * @return      成功返回摄像头句柄，失败返回 NULL
 */
camera_t* camera_open(const char* path)
{
    if (!path) return NULL;

    camera_t* camera = (camera_t*)calloc(1, sizeof(camera_t));
    if (!camera) return NULL;

    camera->fd = open(path, O_RDWR);
    if (camera->fd < 0) {
        LOG_ERROR("camera_open(%s) failed: %s", path, strerror(errno));
        free(camera);
        return NULL;
    }

    /* 查询设备能力 */
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (camera_ioctl(camera->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        LOG_ERROR("VIDIOC_QUERYCAP(%s) failed: %s", path, strerror(errno));
        close(camera->fd);
        free(camera);
        return NULL;
    }

    /* 识别捕获类型：优先 MPLANE */
    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
        camera->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
        camera->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    } else {
        LOG_ERROR("camera_open(%s): device not a video capture device", path);
        close(camera->fd);
        free(camera);
        return NULL;
    }

    LOG_INFO("Camera opened: %s (driver=%s card=%s)",
             path, cap.driver, cap.card);
    return camera;
}

/**
 * 关闭摄像头并释放资源。
 *
 * 若仍在采集状态，先执行 camera_stop 再关闭设备。
 *
 * @param camera  摄像头句柄，可为 NULL（无操作）
 */
void camera_close(camera_t* camera)
{
    if (!camera) return;

    if (camera->streaming) camera_stop(camera);

    if (camera->fd >= 0) close(camera->fd);
    free(camera);
}

/**
 * 按驱动名/卡片名子串查找设备节点路径。
 *
 * 遍历 /dev/video*，用 VIDIOC_QUERYCAP 读取 driver/card 名，
 * 匹配 card_substr 的即为目标节点。用于替代硬编码设备号
 * （rkisp 节点号在重启后可能漂移）。
 *
 * @param card_substr  卡片名子串，如 "rkisp_mainpath" / "rkisp_selfpath"
 * @param path         输出参数，找到的设备路径
 * @param cap          path 缓冲区容量
 * @return             HW_OK 成功，HW_ERR_DEV_NOT_FOUND 未找到
 */
hw_err_t camera_find_by_card(const char* card_substr, char* path, size_t cap)
{
    if (!card_substr || !path || cap == 0) return HW_ERR_PARAM;

    DIR* dir = opendir("/dev");
    if (!dir) {
        LOG_ERROR("opendir(/dev) failed: %s", strerror(errno));
        return HW_ERR_IO;
    }

    hw_err_t ret = HW_ERR_DEV_NOT_FOUND;
    struct dirent* ent;

    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "video", 5) != 0) continue;

        char dev_path[64];
        snprintf(dev_path, sizeof(dev_path), "/dev/%s", ent->d_name);

        int fd = open(dev_path, O_RDWR);
        if (fd < 0) continue;

        struct v4l2_capability capinfo;
        memset(&capinfo, 0, sizeof(capinfo));
        if (camera_ioctl(fd, VIDIOC_QUERYCAP, &capinfo) == 0) {
            if (strstr((const char*)capinfo.card, card_substr) != NULL) {
                snprintf(path, cap, "%s", dev_path);
                ret = HW_OK;
                close(fd);
                break;
            }
        }
        close(fd);
    }

    closedir(dir);

    if (ret != HW_OK) {
        LOG_ERROR("camera_find_by_card(%s): device not found", card_substr);
    } else {
        LOG_INFO("camera_find_by_card(%s) -> %s", card_substr, path);
    }
    return ret;
}

/**
 * 设置采集格式（分辨率 + 像素格式）。
 *
 * 内部将单平面格式自动转为 MPLANE 结构（单 plane）提交；
 * 回读实际设置并计算 stride 与帧大小。
 *
 * @param camera  摄像头句柄
 * @param width   帧宽（像素），须为驱动支持的步长倍数
 * @param height  帧高（像素）
 * @param fourcc  V4L2 像素格式，如 V4L2_PIX_FMT_NV12
 * @return        HW_OK 成功，HW_ERR_PARAM/IO 失败
 */
hw_err_t camera_set_format(camera_t* camera, uint32_t width, uint32_t height,
                           uint32_t fourcc)
{
    if (!camera || width == 0 || height == 0) return HW_ERR_PARAM;

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = (__u32)camera->type;

    if (camera->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        fmt.fmt.pix_mp.width        = width;
        fmt.fmt.pix_mp.height       = height;
        fmt.fmt.pix_mp.pixelformat  = fourcc;
        fmt.fmt.pix_mp.field        = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.num_planes   = 1;
    } else {
        fmt.fmt.pix.width       = width;
        fmt.fmt.pix.height      = height;
        fmt.fmt.pix.pixelformat = fourcc;
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    }

    if (camera_ioctl(camera->fd, VIDIOC_S_FMT, &fmt) < 0) {
        LOG_ERROR("VIDIOC_S_FMT(%ux%u, fourcc=%u) failed: %s",
                  width, height, fourcc, strerror(errno));
        return HW_ERR_IO;
    }

    /* 回读实际格式 */
    if (camera->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        camera->width   = fmt.fmt.pix_mp.width;
        camera->height  = fmt.fmt.pix_mp.height;
        camera->fourcc  = fmt.fmt.pix_mp.pixelformat;
        camera->stride  = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
    } else {
        camera->width   = fmt.fmt.pix.width;
        camera->height  = fmt.fmt.pix.height;
        camera->fourcc  = fmt.fmt.pix.pixelformat;
        camera->stride  = fmt.fmt.pix.bytesperline;
    }

    /* 帧大小：YUV 4:2:0 = w*h*3/2；4:2:2 = w*h*2；否则按 stride*h */
    if (camera_is_420(camera->fourcc)) {
        camera->frame_size = camera->stride * camera->height * 3 / 2;
    } else {
        camera->frame_size = camera->stride * camera->height * 2;
    }

    LOG_INFO("Camera format set: %ux%u fourcc=%u stride=%u frame=%zu",
             camera->width, camera->height, camera->fourcc,
             camera->stride, camera->frame_size);
    return HW_OK;
}

/**
 * 请求 mmap 缓冲并开始采集。
 *
 * 申请 4 个 mmap 缓冲全部入队，执行 STREAMON。
 *
 * @param camera  摄像头句柄
 * @return        HW_OK 成功，HW_ERR_IO 失败
 */
hw_err_t camera_start(camera_t* camera)
{
    if (!camera) return HW_ERR_PARAM;
    if (camera->streaming) return HW_OK;

    /* 请求缓冲 */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = CAMERA_MAX_BUFS;
    req.type   = (__u32)camera->type;
    req.memory = V4L2_MEMORY_MMAP;

    if (camera_ioctl(camera->fd, VIDIOC_REQBUFS, &req) < 0) {
        LOG_ERROR("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return HW_ERR_IO;
    }
    camera->nbufs = (req.count < CAMERA_MAX_BUFS) ? (int)req.count : CAMERA_MAX_BUFS;

    if (camera->nbufs < 2) {
        LOG_ERROR("VIDIOC_REQBUFS: only %d buffers available", camera->nbufs);
        return HW_ERR_IO;
    }

    /* mmap 映射全部缓冲 */
    for (int i = 0; i < camera->nbufs; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));

        if (camera->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            struct v4l2_plane planes[1];
            memset(planes, 0, sizeof(planes));
            buf.type     = (__u32)camera->type;
            buf.memory   = V4L2_MEMORY_MMAP;
            buf.index    = (__u32)i;
            buf.m.planes = planes;
            buf.length   = 1;

            if (camera_ioctl(camera->fd, VIDIOC_QUERYBUF, &buf) < 0) {
                LOG_ERROR("VIDIOC_QUERYBUF(%d) failed: %s", i, strerror(errno));
                return HW_ERR_IO;
            }

            camera->bufs[i].length = planes[0].length;
            camera->bufs[i].mem = mmap(NULL, planes[0].length,
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       camera->fd, planes[0].m.mem_offset);
        } else {
            buf.type   = (__u32)camera->type;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = (__u32)i;

            if (camera_ioctl(camera->fd, VIDIOC_QUERYBUF, &buf) < 0) {
                LOG_ERROR("VIDIOC_QUERYBUF(%d) failed: %s", i, strerror(errno));
                return HW_ERR_IO;
            }

            camera->bufs[i].length = buf.length;
            camera->bufs[i].mem = mmap(NULL, buf.length,
                                       PROT_READ | PROT_WRITE, MAP_SHARED,
                                       camera->fd, buf.m.offset);
        }

        if (camera->bufs[i].mem == MAP_FAILED) {
            LOG_ERROR("mmap(%d) failed: %s", i, strerror(errno));
            return HW_ERR_IO;
        }
    }

    /* 全部入队 */
    for (int i = 0; i < camera->nbufs; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));

        if (camera->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            struct v4l2_plane planes[1];
            memset(planes, 0, sizeof(planes));
            buf.type     = (__u32)camera->type;
            buf.memory   = V4L2_MEMORY_MMAP;
            buf.index    = (__u32)i;
            buf.m.planes = planes;
            buf.length   = 1;
        } else {
            buf.type   = (__u32)camera->type;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index  = (__u32)i;
        }

        if (camera_ioctl(camera->fd, VIDIOC_QBUF, &buf) < 0) {
            LOG_ERROR("VIDIOC_QBUF(%d) failed: %s", i, strerror(errno));
            return HW_ERR_IO;
        }
    }

    /* 开始采集 */
    int type = camera->type;
    if (camera_ioctl(camera->fd, VIDIOC_STREAMON, &type) < 0) {
        LOG_ERROR("VIDIOC_STREAMON failed: %s", strerror(errno));
        return HW_ERR_IO;
    }

    camera->streaming = 1;
    LOG_INFO("Camera streaming started (%d bufs)", camera->nbufs);
    return HW_OK;
}

/**
 * 抓取一帧数据并拷贝到调用者缓冲区。
 *
 * 阻塞等待驱动产出帧，取回后立即重新入队，帧数据拷贝至 dst。
 *
 * @param camera  摄像头句柄（须已 camera_start）
 * @param dst     输出缓冲区，容量须 >= camera_frame_size()
 * @param cap     dst 容量
 * @param size    输出参数，实际拷贝的字节数
 * @return        HW_OK 成功，HW_ERR_IO 失败（丢帧/超时等）
 */
hw_err_t camera_grab(camera_t* camera, uint8_t* dst, size_t cap, size_t* size)
{
    if (!camera || !dst || !size || !camera->streaming) return HW_ERR_PARAM;
    if (cap < camera->frame_size) return HW_ERR_PARAM;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));

    struct v4l2_plane planes[1];
    memset(planes, 0, sizeof(planes));

    if (camera->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        buf.type     = (__u32)camera->type;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.m.planes = planes;
        buf.length   = 1;
    } else {
        buf.type   = (__u32)camera->type;
        buf.memory = V4L2_MEMORY_MMAP;
    }

    /* 阻塞等待一帧 */
    if (camera_ioctl(camera->fd, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) {
            LOG_WARN("camera_grab: no frame available (EAGAIN)");
            return HW_ERR_IO;
        }
        LOG_ERROR("VIDIOC_DQBUF failed: %s", strerror(errno));
        return HW_ERR_IO;
    }

    /* 取出平面数据地址（MPLANE 为 planes[0]） */
    void* src;
    if (camera->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        src = camera->bufs[buf.index].mem;
    } else {
        src = camera->bufs[buf.index].mem;
    }

    memcpy(dst, src, camera->frame_size);
    *size = camera->frame_size;

    /* 重新入队 */
    if (camera_ioctl(camera->fd, VIDIOC_QBUF, &buf) < 0) {
        LOG_ERROR("VIDIOC_QBUF(re) failed: %s", strerror(errno));
        return HW_ERR_IO;
    }

    return HW_OK;
}

/**
 * 停止采集并释放 mmap 缓冲。
 *
 * 执行 STREAMOFF 并 munmap 全部缓冲，streaming 状态复位。
 *
 * @param camera  摄像头句柄
 */
void camera_stop(camera_t* camera)
{
    if (!camera || !camera->streaming) return;

    int type = camera->type;
    if (camera_ioctl(camera->fd, VIDIOC_STREAMOFF, &type) < 0) {
        LOG_WARN("VIDIOC_STREAMOFF failed: %s", strerror(errno));
    }

    for (int i = 0; i < camera->nbufs; i++) {
        if (camera->bufs[i].mem && camera->bufs[i].mem != MAP_FAILED) {
            munmap(camera->bufs[i].mem, camera->bufs[i].length);
        }
        camera->bufs[i].mem = NULL;
    }

    camera->nbufs = 0;
    camera->streaming = 0;
    LOG_INFO("Camera streaming stopped");
}

/**
 * 获取单帧数据大小（字节）。
 *
 * @param camera  摄像头句柄（须已 camera_set_format）
 * @return        帧大小字节数；句柄无效时返回 0
 */
size_t camera_frame_size(camera_t* camera)
{
    return camera ? camera->frame_size : 0;
}

/**
 * 获取帧宽（像素）。
 *
 * @param camera  摄像头句柄
 * @return        帧宽；句柄无效时返回 0
 */
uint32_t camera_width(camera_t* camera)
{
    return camera ? camera->width : 0;
}

/**
 * 获取帧高（像素）。
 *
 * @param camera  摄像头句柄
 * @return        帧高；句柄无效时返回 0
 */
uint32_t camera_height(camera_t* camera)
{
    return camera ? camera->height : 0;
}

/**
 * 获取行字节数（bytesperline）。
 *
 * 行填充存在时 stride > width（如 rkisp 按 32 字节对齐）。
 *
 * @param camera  摄像头句柄
 * @return        每行字节数；句柄无效时返回 0
 */
uint32_t camera_stride(camera_t* camera)
{
    return camera ? camera->stride : 0;
}
