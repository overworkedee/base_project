/**
 * rtsp_launch.c — 极简 RTSP 推流服务（等价于 gst-rtsp-server 的 test-launch）
 *
 * 在开发板上用原生 gcc 编译（板上已装 gstreamer 开发环境）：
 *
 *   gcc rtsp_launch.c -o rtsp-launch \
 *       $(pkg-config --cflags --libs gstreamer-1.0 gst-rtsp-server-1.0)
 *
 * 用法：
 *   rtsp-launch "( v4l2src device=/dev/video12 ! video/x-raw,format=NV12,
 *                 width=1280,height=720,framerate=30/1 !
 *                 mpph264enc bitrate=3000000 ! rtph264pay name=pay0 pt=96 )"
 *
 * 默认监听 8554 端口，挂载点 "/"，上位机访问 rtsp://<ip>:8554/
 *
 * 注意：Ubuntu/Debian 仓库的 gstreamer1.0-rtsp 包不提供 test-launch 二进制，
 * 因此本工具作为替代，由 app_camera 通过 RTSP_LAUNCH_CANDS 查找调用。
 */

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

int main(int argc, char* argv[])
{
    GMainLoop* loop;
    GstRTSPServer* server;
    GstRTSPMountPoints* mounts;
    GstRTSPMediaFactory* factory;

    if (argc < 2) {
        g_printerr("usage: %s <launch-line>\n", argv[0]);
        return 1;
    }

    gst_init(&argc, &argv);

    loop = g_main_loop_new(NULL, FALSE);

    server = gst_rtsp_server_new();
    gst_rtsp_server_set_service(server, "8554");

    mounts = gst_rtsp_server_get_mount_points(server);

    factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, argv[1]);
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    gst_rtsp_mount_points_add_factory(mounts, "/", factory);
    g_object_unref(mounts);

    if (gst_rtsp_server_attach(server, NULL) == 0) {
        g_printerr("failed to attach RTSP server\n");
        return 1;
    }

    g_print("stream ready at rtsp://127.0.0.1:8554/\n");

    g_main_loop_run(loop);

    return 0;
}
