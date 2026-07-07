// ╔══════════════════════════════════════════════════════════╗
// ║  主程序 — 三线程管线: 采集 → 推理 → 安全 → RTSP推流     ║
// ║  RK3588 NPU + MIPI 摄像头 + MPP 硬编码 + RTSP           ║
// ╚══════════════════════════════════════════════════════════╝

#include <stdio.h>
#include "rknn_api.h"
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "core/preprocess.h"
#include "core/postprocess.h"
#include "core/pipeline.h"
#include "io/coco_names.h"
#include "io/json_output.h"
#include "io/mqtt_publisher.h"
#include "io/camera_capture.h"
#include "io/overlay.h"
#include "hw/rga_preprocess.h"
#include "safety/safety_state.h"
#include "config.h"
#include <atomic>
#include <chrono>
#include <csignal>

using namespace cv;
using namespace std;

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);

    if (argc < 2) { printf("用法: %s <model.rknn> [image.jpg]\n", argv[0]); return 1; }
    char *model_path = argv[1];
    char *image_path = argc >= 3 ? argv[2] : nullptr;

    // ── MQTT ──
    rknn_context context; int ret;
    struct mosquitto* mqtt = mqtt_init("rk3588", MQTT_BROKER_IP, MQTT_PORT);
    if (!mqtt) printf("⚠️ MQTT 连不上, 继续本地运行\n");
    if (mqtt) {
        mqtt_subscribe(mqtt, MQTT_TOPIC_CMD, [](const string& cmd) {
            printf("\n📩 [收到指令] %s\n", cmd.c_str());
        });
    }

    // ── 加载 RKNN 模型 ──
    ret = rknn_init(&context, model_path, 0, 0, NULL);
    if (ret) { printf("rknn_init fail! ret=%d\n", ret); return ret; }

    rknn_tensor_attr input_attr[1], output_attr[2];
    memset(input_attr,  0, sizeof(input_attr));
    memset(output_attr, 0, sizeof(output_attr));
    rknn_query(context, RKNN_QUERY_INPUT_ATTR,  input_attr,  sizeof(input_attr));
    rknn_query(context, RKNN_QUERY_OUTPUT_ATTR, output_attr, sizeof(output_attr));
    printf("input  type: %s\n", get_type_string(input_attr[0].type));
    printf("out[0] type: %s  dims=[%d,%d,%d,%d]\n",
        get_type_string(output_attr[0].type),
        output_attr[0].dims[0], output_attr[0].dims[1],
        output_attr[0].dims[2], output_attr[0].dims[3]);
    printf("out[1] type: %s  dims=[%d,%d,%d,%d]\n",
        get_type_string(output_attr[1].type),
        output_attr[1].dims[0], output_attr[1].dims[1],
        output_attr[1].dims[2], output_attr[1].dims[3]);

#if USE_ZCOPY
    rknn_tensor_mem *zcopy_in = rknn_create_mem(context, input_attr[0].size_with_stride);
    if (zcopy_in && rknn_set_io_mem(context, zcopy_in, &input_attr[0]) == 0) {
        printf("✅ RKNN 零拷贝输入已绑定 (%d bytes)\n", input_attr[0].size_with_stride);
    } else {
        printf("⚠️  零拷贝不可用, 回退\n");
        if (zcopy_in) { rknn_destroy_mem(context, zcopy_in); zcopy_in = nullptr; }
    }
#else
    rknn_tensor_mem *zcopy_in = nullptr;
#endif

#if USE_VIDEO
    printf("🎬 图片循环模式: %s\n", VIDEO_PATH);
#else
    CameraCapture cam;
    if (!cam.open(CAM_DEVICE, CAM_WIDTH, CAM_HEIGHT))
        printf("❌ 摄像头失败, 回退到图片序列模式\n");
#endif

    using ns = chrono::nanoseconds;

    { char cfg[128]; snprintf(cfg, sizeof(cfg),
        "{\"rga\":%d,\"mt\":%d,\"rtsp\":%d}", USE_RGA, USE_MULTITHREAD, USE_RTSP);
      mqtt_send(mqtt, "robot/config", cfg); }

#if USE_MULTITHREAD
    struct PreppedFrame {
        Mat img; int id;
        chrono::steady_clock::time_point capture_tp;
    };
    struct InferResult {
        vector<Box> boxes; Mat img; int id;
        chrono::steady_clock::time_point capture_tp;
    };

    SafeQueue<PreppedFrame> q1(QUEUE_SIZE);
    SafeQueue<InferResult>  q2(QUEUE_SIZE);
    atomic<bool>      running{true};
    atomic<long long> acc_capture_ns{0};
    atomic<long long> acc_infer_ns{0};
    atomic<long long> acc_post_ns{0};
    atomic<int>       perf_cnt{0};

    // ── T1: 采集 ──
    thread t1([&]() {
        Mat img(MODEL_H, MODEL_W, CV_8UC3);
        int id = 0;
        while (running) {
            auto t0 = chrono::steady_clock::now();
#if USE_VIDEO
            Mat frame = imread(VIDEO_PATH);
            resize(frame, img, Size(MODEL_W, MODEL_H));
            cvtColor(img, img, COLOR_BGR2RGB);
#elif USE_RGA
            uint8_t* raw = nullptr; uint32_t fmt = 0;
            if (!cam.read_raw(raw, fmt)) break;
            rga_uyvy_to_rgb(raw, cam.get_width(), cam.get_height(), img);
            cam.qbuf();
#else
            Mat frame;
            if (!cam.read(frame)) break;
            resize(frame, img, Size(MODEL_W, MODEL_H));
            cvtColor(img, img, COLOR_BGR2RGB);
#endif
            rotate(img, img, ROTATE_90_COUNTERCLOCKWISE);
            PreppedFrame pf;
            pf.img = img.clone(); pf.id = id++;
            pf.capture_tp = chrono::steady_clock::now();
            acc_capture_ns += ns(chrono::steady_clock::now() - t0).count();
            q1.push(move(pf));
        }
        PreppedFrame stop; stop.id = -1; q1.push(move(stop));
        running = false;
    });

    // ── T2: 推理 ──
    thread t2([&]() {
        while (running) {
            PreppedFrame pf = q1.pop();
            if (pf.id < 0 || !running) break;
            auto t0 = chrono::steady_clock::now();
            auto t_npu = chrono::steady_clock::now();
            InferResult ir;
            if (zcopy_in) {
                memcpy(zcopy_in->virt_addr, pf.img.data, MODEL_BYTES);
                rknn_run(context, NULL);
                t_npu = chrono::steady_clock::now();
            } else {
                rknn_input inputs[1] = {{0}};
                inputs[0].index = 0; inputs[0].buf = pf.img.data;
                inputs[0].size = MODEL_BYTES;
                inputs[0].type = RKNN_TENSOR_UINT8;
                inputs[0].fmt = RKNN_TENSOR_NHWC;
                rknn_inputs_set(context, 1, inputs);
                rknn_run(context, NULL);
                t_npu = chrono::steady_clock::now();
            }
            rknn_output outputs[2] = {};
            outputs[0].want_float = 1; outputs[1].want_float = 1;
            rknn_outputs_get(context, 2, outputs, NULL);
            ir.boxes = postprocess_split((float*)outputs[0].buf, (float*)outputs[1].buf, 0.45f);
            rknn_outputs_release(context, 2, outputs);
            ir.img = move(pf.img); ir.id = pf.id;
            ir.capture_tp = pf.capture_tp;
            auto t2_end = chrono::steady_clock::now();
            acc_infer_ns += ns(t_npu - t0).count();
            acc_post_ns  += ns(t2_end - t_npu).count();
            q2.push(move(ir));
        }
        InferResult stop; stop.id = -1; q2.push(move(stop));
    });

    // ── 主线程 ──
    printf("\n📡 RTSP 推流: " RTSP_URL "\n\n");
    int frame_id = 0;
    auto perf_t0 = chrono::steady_clock::now();
    static double smooth_fps = 0;
    static auto  prev_tp = chrono::steady_clock::now();

    while (running) {
        InferResult ir = q2.pop();
        if (ir.id < 0) break;

        auto safety = safety_check(ir.boxes, Action::MOVE_FORWARD);
        auto now = chrono::steady_clock::now();
        double e2e_ms = ns(now - ir.capture_tp).count() / 1e6;
        double dt_ms = ns(now - prev_tp).count() / 1e6; prev_tp = now;
        double instant_fps = (dt_ms > 0) ? 1000.0 / dt_ms : 0;
        smooth_fps = smooth_fps * (1.0 - FPS_EMA_ALPHA) + instant_fps * FPS_EMA_ALPHA;

        int cnt = perf_cnt.load();
        double capture_ms = cnt > 0 ? (acc_capture_ns.load() / cnt) / 1e6 : 0;
        double infer_ms   = cnt > 0 ? (acc_infer_ns.load()   / cnt) / 1e6 : 0;
        double nms_ms     = cnt > 0 ? (acc_post_ns.load()    / cnt) / 1e6 : 0;

#if USE_RTSP
        static FILE* stream_pipe = fopen(STREAM_PIPE, "wb");
        Mat vis = ir.img.clone();
        cvtColor(vis, vis, COLOR_RGB2BGR);
        draw_boxes(vis, ir.boxes);
        draw_perf_overlay(vis, smooth_fps, e2e_ms, capture_ms, infer_ms, nms_ms, ir.id);
        write_nv12_pipe(vis, stream_pipe);
#endif

        ++perf_cnt;
        if (ir.id % PERF_INTERVAL == 0 && ir.id > 0) {
            double fps_30 = PERF_INTERVAL / (ns(now - perf_t0).count() / 1e9);
            double cap_avg = (acc_capture_ns.load() / PERF_INTERVAL) / 1e6;
            double npu_avg = (acc_infer_ns.load()   / PERF_INTERVAL) / 1e6;
            double nms_avg = (acc_post_ns.load()    / PERF_INTERVAL) / 1e6;
            printf("⚡ [FPS %.1f] 采集:%.1fms NPU:%.1fms NMS:%.1fms\n", fps_30, cap_avg, npu_avg, nms_avg);
            char buf[256]; snprintf(buf, sizeof(buf),
                "{\"fps\":%.1f,\"capture_ms\":%.1f,\"npu_ms\":%.1f,\"nms_ms\":%.1f}",
                fps_30, cap_avg, npu_avg, nms_avg);
            mqtt_send(mqtt, MQTT_TOPIC_PERF, buf);
            acc_capture_ns = acc_infer_ns = acc_post_ns = 0; perf_cnt = 0; perf_t0 = now;
        }
        frame_id++;
    }

    running = false; t1.join(); t2.join();

#else  // ── 单线程模式 ──
    printf("\n📡 单线程模式\n\n");
    Mat frame, img(MODEL_H, MODEL_W, CV_8UC3);
    int id = 0;
    auto perf_t0 = chrono::steady_clock::now();
    double smooth_fps = 0;
    auto prev_tp = chrono::steady_clock::now();
    double acc_cap = 0, acc_npu = 0, acc_nms = 0;

    while (true) {
        auto t_cap = chrono::steady_clock::now();
#if USE_VIDEO
        frame = imread(VIDEO_PATH);
        resize(frame, img, Size(MODEL_W, MODEL_H));
        cvtColor(img, img, COLOR_BGR2RGB);
#elif USE_RGA
        uint8_t* raw = nullptr; uint32_t fmt = 0;
        if (!cam.read_raw(raw, fmt)) break;
        rga_uyvy_to_rgb(raw, cam.get_width(), cam.get_height(), img);
        cam.qbuf();
#else
        if (!cam.read(frame)) break;
        resize(frame, img, Size(MODEL_W, MODEL_H));
        cvtColor(img, img, COLOR_BGR2RGB);
#endif
        rotate(img, img, ROTATE_90_COUNTERCLOCKWISE);
        auto cap_tp = chrono::steady_clock::now();
        double cap_ms = ns(cap_tp - t_cap).count() / 1e6;

        if (zcopy_in) {
            memcpy(zcopy_in->virt_addr, img.data, MODEL_BYTES);
            rknn_run(context, NULL);
        } else {
            rknn_input inputs[1] = {{0}};
            inputs[0].index = 0; inputs[0].buf = img.data;
            inputs[0].size = MODEL_BYTES;
            inputs[0].type = RKNN_TENSOR_UINT8;
            inputs[0].fmt = RKNN_TENSOR_NHWC;
            rknn_inputs_set(context, 1, inputs);
            rknn_run(context, NULL);
        }
        auto t_npu = chrono::steady_clock::now();
        rknn_output outputs[2] = {};
        outputs[0].want_float = 1; outputs[1].want_float = 1;
        rknn_outputs_get(context, 2, outputs, NULL);
        auto boxes = postprocess_split((float*)outputs[0].buf, (float*)outputs[1].buf, 0.25f);
        rknn_outputs_release(context, 2, outputs);
        auto t_post = chrono::steady_clock::now();
        double npu_ms = ns(t_npu - cap_tp).count() / 1e6;
        double nms_ms = ns(t_post - t_npu).count() / 1e6;

        auto safety = safety_check(boxes, Action::MOVE_FORWARD);
        double e2e_ms = ns(t_post - cap_tp).count() / 1e6;
        double dt_ms = ns(t_post - prev_tp).count() / 1e6; prev_tp = t_post;
        double instant_fps = (dt_ms > 0) ? 1000.0 / dt_ms : 0;
        smooth_fps = smooth_fps * (1.0 - FPS_EMA_ALPHA) + instant_fps * FPS_EMA_ALPHA;

#if USE_RTSP
        static FILE* stream_pipe = fopen(STREAM_PIPE, "wb");
        cvtColor(img, img, COLOR_RGB2BGR);
        draw_boxes(img, boxes);
        draw_perf_overlay(img, smooth_fps, e2e_ms, cap_ms, npu_ms, nms_ms, id);
        write_nv12_pipe(img, stream_pipe);
#endif
        acc_cap += cap_ms; acc_npu += npu_ms; acc_nms += nms_ms;
        if (id % PERF_INTERVAL == 0 && id > 0) {
            double fps_30 = PERF_INTERVAL / (ns(t_post - perf_t0).count() / 1e9);
            printf("⚡ [FPS %.1f] 采集:%.1fms NPU:%.1fms NMS:%.1fms\n",
                   fps_30, acc_cap/PERF_INTERVAL, acc_npu/PERF_INTERVAL, acc_nms/PERF_INTERVAL);
            char buf[256]; snprintf(buf, sizeof(buf),
                "{\"fps\":%.1f,\"capture_ms\":%.1f,\"npu_ms\":%.1f,\"nms_ms\":%.1f}",
                fps_30, acc_cap/PERF_INTERVAL, acc_npu/PERF_INTERVAL, acc_nms/PERF_INTERVAL);
            mqtt_send(mqtt, MQTT_TOPIC_PERF, buf);
            acc_cap = acc_npu = acc_nms = 0; perf_t0 = t_post;
        }
        printf("[帧%d] %zu物体 %s | %.0fms\n", id, boxes.size(), safety.reason, e2e_ms);
        id++;
    }
#endif

    if (zcopy_in) rknn_destroy_mem(context, zcopy_in);
    mqtt_cleanup(mqtt);
    rknn_destroy(context);
    return 0;
}
