/**
 * app_camera.c — 相机应用模块
 *
 * 整合 V4L2 摄像头采集（hw/dev_camera）与视觉处理（modules/vision），
 * 并管理 RTSP 视频流服务进程（板上 gst-rtsp-launch 工具）：
 *
 *   - 拍照：从 rkisp mainpath 抓一帧 NV12 → vision 转 BGR →
 *           灰度/Canny → 统计非零像素 → 存 JPG
 *   - RTSP：fork+exec 拉起 gst-rtsp-launch，管道为
 *           selfpath 720p NV12 → mpph264enc 硬件编码 → rtph264pay
 *
 * 设备路径通过 camera_find_by_card 动态解析（rkisp 节点号会漂移）。
 * mainpath 与 selfpath 由 ISP 同一帧流分发，可并行工作。
 */

#include "app_camera.h"
#include "cmd/cmd_server.h"
#include "cmd/cmd_frame.h"
#include "hw/dev/dev_camera.h"
#include "vision/vision.h"
#include "log/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <linux/videodev2.h>

/* ── 配置常量 ───────────────────────────────────────────────────────── */

#define CAMERA_CARD_MAIN   "rkisp_mainpath"   /* 拍照节点卡片名 */
#define CAMERA_CARD_SELF   "rkisp_selfpath"   /* 视频流节点卡片名 */
#define CAMERA_DEV_PATH    "/dev/video-camera0" /* 备选：稳定符号链接 */
#define CAMERA_DEFAULT_W   1280
#define CAMERA_DEFAULT_H   720
#define CAMERA_SNAP_DIR    "/tmp"             /* 拍照输出目录 */
#define RTSP_PORT          8554               /* RTSP 监听端口 */

/* gst-rtsp-launch 二进制候选路径（发行版差异） */
static const char* const RTSP_LAUNCH_CANDS[] = {
    "/usr/local/bin/rtsp-launch",  /* 本项目自编译工具（tools/rtsp_launch.c） */
    "/usr/bin/gst-rtsp-launch",
    "/usr/bin/test-launch",
    NULL
};

/* ── 内部结构 ───────────────────────────────────────────────────────── */

struct app_camera {
    pthread_t    thread;      /* 采集/监控线程 */
    int          started;     /* 线程是否已启动 */
    volatile int running;     /* 线程运行标志 */
    volatile int busy;        /* 是否正在拍照处理 */
    pid_t        rtsp_pid;    /* RTSP 子进程 PID（-1=未运行） */
    char         main_path[64];  /* mainpath 节点路径 */
    char         self_path[64];  /* selfpath 节点路径 */
    const app_cmd_svc_t* svc;    /* 命令服务能力表（回调注入） */
};

/* 前向声明（app_camera_create 通过能力表注册它） */
static void cmd_handler_camera(const cmd_frame_t* req, cmd_conn_t* conn,
                               void* ctx);

/* ── 内部辅助函数 ───────────────────────────────────────────────────── */

/**
 * 查找 gst-rtsp-launch 可执行文件。
 *
 * @return  找到的完整路径，未找到返回 NULL
 */
static const char* rtsp_launch_bin(void)
{
    for (int i = 0; RTSP_LAUNCH_CANDS[i] != NULL; i++) {
        if (access(RTSP_LAUNCH_CANDS[i], X_OK) == 0) {
            return RTSP_LAUNCH_CANDS[i];
        }
    }
    return NULL;
}

/**
 * 启动 RTSP 推流子进程。
 *
 * fork + execl 拉起 rtsp-launch，管道为：
 *   v4l2src selfpath 720p NV12 ! mpph264enc 3Mbps ! rtph264pay
 * 子进程处理：
 *   - setsid 脱离会话（避免终端信号/进程组影响）
 *   - stdin 重定向 /dev/null，stdout/stderr 重定向日志文件
 *   - 忽略 SIGPIPE（客户端断开时 RTP 写 socket 会触发）
 *
 * @param camera  相机应用模块（含已解析的 self_path）
 * @return        0 成功（rtsp_pid 已设置），-1 失败
 */
static int rtsp_start(app_camera_t* camera)
{
    if (camera->rtsp_pid > 0) {
        LOG_WARN("RTSP already running (pid=%d)", camera->rtsp_pid);
        return -1;
    }

    const char* bin = rtsp_launch_bin();
    if (!bin) {
        LOG_ERROR("RTSP launcher not found (rtsp-launch/gst-rtsp-launch missing)");
        return -1;
    }

    if (camera->self_path[0] == '\0') {
        LOG_ERROR("RTSP: selfpath device not resolved");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("RTSP fork() failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        /* 子进程：脱离会话，防止随父进程会话退出 */
        if (setsid() < 0) {
            /* 已是会话首领时失败，可忽略 */
        }

        /* stdin → /dev/null，stdout/stderr → 日志文件 */
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            close(devnull);
        }
        int logfd = open("/tmp/rtsp_launch.log",
                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (logfd >= 0) {
            dup2(logfd, STDOUT_FILENO);
            dup2(logfd, STDERR_FILENO);
            close(logfd);
        }

        /* 客户端断开时避免 SIGPIPE 终止进程 */
        signal(SIGPIPE, SIG_IGN);

        /* 管道串（rtsp-launch 接收完整 launch 描述） */
        char pipeline[512];
        snprintf(pipeline, sizeof(pipeline),
                 "( v4l2src device=%s ! video/x-raw,format=NV12,"
                 "width=1280,height=720,framerate=30/1 ! "
                 "mpph264enc bitrate=3000000 ! "
                 "rtph264pay name=pay0 pt=96 )",
                 camera->self_path);

        execl(bin, bin, pipeline, (char*)NULL);

        /* execl 失败才会走到这里 */
        dprintf(STDERR_FILENO, "exec %s failed: %s\n", bin, strerror(errno));
        _exit(127);
    }

    camera->rtsp_pid = pid;
    LOG_INFO("RTSP stream started (pid=%d, rtsp://<ip>:%d/)",
             pid, RTSP_PORT);
    return 0;
}

/**
 * 停止 RTSP 子进程并回收。
 *
 * 发送 SIGTERM，等待 3 秒，超时则 SIGKILL。
 *
 * @param camera  相机应用模块
 */
static void rtsp_stop(app_camera_t* camera)
{
    if (camera->rtsp_pid <= 0) return;

    LOG_INFO("Stopping RTSP stream (pid=%d)", camera->rtsp_pid);
    kill(camera->rtsp_pid, SIGTERM);

    int status = 0;
    for (int i = 0; i < 30; i++) {
        if (waitpid(camera->rtsp_pid, &status, WNOHANG) == camera->rtsp_pid) {
            break;
        }
        usleep(100000);
    }

    /* 超时强制杀 */
    if (kill(camera->rtsp_pid, 0) == 0) {
        LOG_WARN("RTSP process didn't exit, sending SIGKILL");
        kill(camera->rtsp_pid, SIGKILL);
        waitpid(camera->rtsp_pid, &status, 0);
    }

    camera->rtsp_pid = -1;
}

/* ── 采集/监控线程 ───────────────────────────────────────────────────── */

/**
 * 相机监控线程。
 *
 * 周期性轮询 RTSP 子进程状态，异常退出时清理并记录日志。
 *
 * @param arg  app_camera_t* 上下文
 * @return     NULL
 * @note       退出条件：app_camera_stop 将 running 置 0
 */
static void* camera_thread(void* arg)
{
    app_camera_t* camera = (app_camera_t*)arg;

    LOG_INFO("Camera thread started");

    while (camera->running) {
        /* 监控 RTSP 子进程是否意外退出 */
        if (camera->rtsp_pid > 0) {
            int status = 0;
            pid_t ret = waitpid(camera->rtsp_pid, &status, WNOHANG);
            if (ret == camera->rtsp_pid) {
                LOG_WARN("RTSP process exited unexpectedly (status=%d)",
                         WEXITSTATUS(status));
                camera->rtsp_pid = -1;
            }
        }
        sleep(1);
    }

    LOG_INFO("Camera thread stopped");
    return NULL;
}

/* ── 生命周期 ───────────────────────────────────────────────────────── */

/**
 * 创建相机应用模块并注册 CMD_CAMERA 处理器。
 *
 * 动态解析 rkisp mainpath/selfpath 节点路径；
 * 通过 svc->register_cmd 将 cmd_handler_camera 注入调度器。
 *
 * @param svc  命令服务能力表（来自 app_cmd_get_svc），可为 NULL
 * @return     成功返回实例指针，失败返回 NULL
 */
app_camera_t* app_camera_create(const app_cmd_svc_t* svc)
{
    app_camera_t* camera = (app_camera_t*)calloc(1, sizeof(app_camera_t));
    if (!camera) return NULL;

    camera->rtsp_pid = -1;
    camera->svc = svc;

    if (svc && svc->register_cmd) {
        svc->register_cmd(svc->owner, CMD_CAMERA, cmd_handler_camera, camera);
    }

    /* 解析设备节点（rkisp 节点号会漂移，按卡片名查找） */
    if (camera_find_by_card(CAMERA_CARD_MAIN, camera->main_path,
                            sizeof(camera->main_path)) != HW_OK) {
        /* 备选：稳定符号链接 */
        if (access(CAMERA_DEV_PATH, R_OK) == 0) {
            snprintf(camera->main_path, sizeof(camera->main_path), "%s",
                     CAMERA_DEV_PATH);
            LOG_INFO("Camera mainpath fallback to %s", camera->main_path);
        } else {
            LOG_WARN("Camera mainpath not found, snapshots unavailable");
        }
    }

    if (camera_find_by_card(CAMERA_CARD_SELF, camera->self_path,
                            sizeof(camera->self_path)) != HW_OK) {
        LOG_WARN("Camera selfpath not found, RTSP stream unavailable");
    }

    return camera;
}

/**
 * 释放相机应用模块。
 *
 * 若 RTSP 仍在运行会一并停止。
 *
 * @param camera  相机应用模块（可为 NULL）
 * @note           调用前必须先 app_camera_stop，否则线程仍在使用上下文
 */
void app_camera_destroy(app_camera_t* camera)
{
    if (!camera) return;

    rtsp_stop(camera);
    free(camera);
}

/* ── 生命周期（线程） ───────────────────────────────────────────────── */

/**
 * 启动相机监控线程，并默认启动 RTSP 推流。
 *
 * RTSP 启动失败仅记录日志，不影响主流程。
 *
 * @param camera  相机应用模块
 * @return        0 成功，-1 失败（参数错误或线程创建失败）
 * @note          重复调用返回 -1（线程已启动）
 */
int app_camera_start(app_camera_t* camera)
{
    if (!camera || camera->started) return -1;

    camera->running = 1;
    if (pthread_create(&camera->thread, NULL, camera_thread, camera) != 0) {
        camera->running = 0;
        return -1;
    }

    camera->started = 1;

    /* 默认启动 RTSP 推流（失败不影响主流程） */
    if (rtsp_start(camera) != 0) {
        LOG_WARN("RTSP auto-start failed, use 'camera rtsp on' to retry");
    }

    return 0;
}

/**
 * 停止监控线程并等待其退出。
 *
 * RTSP 推流进程也会一并停止。
 *
 * @param camera  相机应用模块
 * @note           未启动时无操作；调用后不可再 start（需重新 create）
 */
void app_camera_stop(app_camera_t* camera)
{
    if (!camera || !camera->started) return;

    camera->running = 0;
    pthread_join(camera->thread, NULL);
    camera->started = 0;

    rtsp_stop(camera);
}

/* ── 命令处理器 ─────────────────────────────────────────────────────── */

/**
 * 相机命令处理器（CMD=0x05）。
 *
 * ctx 应为 app_camera_t*。
 *
 * 子命令:
 *   WRITE (0x01): 拍照。PAYLOAD=[w 2B BE, h 2B BE] 可选，缺省 1280x720。
 *                 响应 PAYLOAD=[err 1B, count 4B BE]
 *   READ  (0x02): 查询状态。响应 PAYLOAD=[err 1B, busy 1B, rtsp 1B]
 *   RTSP_START (0x07): 启动推流。响应 PAYLOAD=[err 1B]
 *   RTSP_STOP  (0x08): 停止推流。响应 PAYLOAD=[err 1B]
 *
 * @param req   请求帧
 * @param conn  来源连接
 * @param ctx   app_camera_t* 句柄
 */
static void cmd_handler_camera(const cmd_frame_t* req, cmd_conn_t* conn,
                               void* ctx)
{
    app_camera_t* camera = (app_camera_t*)ctx;

    uint8_t op = cmd_frame_sub_req(req->sub);

    if (op == CMD_SUB_WRITE) {
        /* ── 拍照：抓一帧 NV12 → vision 处理 ── */
        if (!camera || camera->busy) {
            uint8_t err = camera ? CMD_ERR_BUSY : CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        if (camera->main_path[0] == '\0') {
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            LOG_ERROR("Camera: mainpath unavailable");
            return;
        }

        /* 分辨率：PAYLOAD 可选 [w 2B BE, h 2B BE] */
        uint32_t w = CAMERA_DEFAULT_W, h = CAMERA_DEFAULT_H;
        if (req->len >= 4) {
            w = ((uint32_t)req->payload[0] << 8) | req->payload[1];
            h = ((uint32_t)req->payload[2] << 8) | req->payload[3];
        }

        camera->busy = 1;

        /* 打开设备并设置格式 */
        camera_t* cam = camera_open(camera->main_path);
        if (!cam) {
            camera->busy = 0;
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        hw_err_t rc = camera_set_format(cam, w, h, V4L2_PIX_FMT_NV12);
        if (rc != HW_OK) {
            camera_close(cam);
            camera->busy = 0;
            uint8_t err = CMD_ERR_PARAM;
            cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        rc = camera_start(cam);
        if (rc != HW_OK) {
            camera_close(cam);
            camera->busy = 0;
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        /* 抓帧（含 4:2:0 的 stride 填充） */
        uint32_t real_w = camera_width(cam);
        uint32_t real_h = camera_height(cam);
        uint32_t stride = camera_stride(cam);
        size_t frame_size = camera_frame_size(cam);
        uint8_t* frame = (uint8_t*)malloc(frame_size);
        size_t got = 0;
        rc = camera_grab(cam, frame, frame_size, &got);

        camera_stop(cam);
        camera_close(cam);

        if (rc != HW_OK) {
            free(frame);
            camera->busy = 0;
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        /* vision 处理：NV12 → BGR → 灰度 → Canny → 统计 */
        vision_image_t* img = vision_create_from_nv12(frame, (int)real_w,
                                                      (int)real_h, (int)stride);
        free(frame);

        if (!img) {
            camera->busy = 0;
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            LOG_ERROR("Camera: vision_create_from_nv12 failed");
            return;
        }

        vision_to_gray(img);
        vision_canny(img, 50, 150);

        int count = 0;
        vision_count_nonzero(img, &count);
        if (count < 0) count = 0;

        /* 保存 JPG（时间戳命名） */
        char out_path[128];
        snprintf(out_path, sizeof(out_path), "%s/camera_%ld.jpg",
                 CAMERA_SNAP_DIR, (long)time(NULL));
        vision_save(img, out_path);
        vision_destroy(img);

        camera->busy = 0;

        /* 响应 PAYLOAD = [err 1B, count 4B BE] */
        uint8_t pld[5];
        pld[0] = CMD_ERR_OK;
        uint32_t count_be = (uint32_t)count;
        pld[1] = (count_be >> 24) & 0xFF;
        pld[2] = (count_be >> 16) & 0xFF;
        pld[3] = (count_be >> 8) & 0xFF;
        pld[4] = count_be & 0xFF;

        cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 5, .payload = pld };
        cmd_conn_send(conn, &rsp);

        LOG_INFO("Camera: snapshot %ux%u -> %s (%d nonzero pixels)",
                 w, h, out_path, count);

    } else if (op == CMD_SUB_READ) {
        /* 查询相机状态 */
        uint8_t pld[3];
        pld[0] = CMD_ERR_OK;
        pld[1] = camera ? (uint8_t)camera->busy : 0;
        pld[2] = (camera && camera->rtsp_pid > 0) ? 1 : 0;

        cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 3, .payload = pld };
        cmd_conn_send(conn, &rsp);

    } else if (op == CMD_SUB_RTSP_START) {
        /* 启动 RTSP 推流 */
        if (!camera) {
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        uint8_t err = (rtsp_start(camera) == 0) ? CMD_ERR_OK : CMD_ERR_HARDWARE;
        cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);

    } else if (op == CMD_SUB_RTSP_STOP) {
        /* 停止 RTSP 推流 */
        if (!camera) {
            uint8_t err = CMD_ERR_HARDWARE;
            cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                                .len = 1, .payload = &err };
            cmd_conn_send(conn, &rsp);
            return;
        }

        rtsp_stop(camera);
        uint8_t err = CMD_ERR_OK;
        cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);

    } else {
        uint8_t err = CMD_ERR_UNKNOWN_SUB;
        cmd_frame_t rsp = { .cmd = CMD_CAMERA, .sub = cmd_frame_sub_rsp(req->sub),
                            .len = 1, .payload = &err };
        cmd_conn_send(conn, &rsp);
    }
}
