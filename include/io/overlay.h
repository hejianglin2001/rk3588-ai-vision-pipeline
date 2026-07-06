#pragma once
// ╔══════════════════════════════════════════════════════════╗
// ║  画面叠加工具 — 检测框 + 性能指标 + NV12 推流          ║
// ║  从 main.cpp 抽离, 保持主循环可读                      ║
// ╚══════════════════════════════════════════════════════════╝

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <cstdio>
#include <vector>
#include "core/postprocess.h"
#include "io/coco_names.h"

// ── 画检测框 ──
inline void draw_boxes(cv::Mat& bgr, const std::vector<Box>& boxes) {
    for (const auto& b : boxes) {
        cv::rectangle(bgr, cv::Point(b.x1, b.y1), cv::Point(b.x2, b.y2),
                      cv::Scalar(0, 255, 0), 2);
        char label[64];
        snprintf(label, sizeof(label), "%s %.2f",
                 b.cls < 80 ? COCO_NAMES[b.cls] : "?", b.conf);
        cv::putText(bgr, label, cv::Point(b.x1, b.y1 - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
    }
}

// ── 画性能指标叠加 (左上角, 透明单行) ──
inline void draw_perf_overlay(cv::Mat& bgr, double fps, double e2e_ms,
                              double capture_ms, double npu_ms, double nms_ms,
                              int frame_id) {
    char buf[200];
    snprintf(buf, sizeof(buf),
        "FPS:%.1f | E2E:%.0fms | Cap:%.1fms NPU:%.1fms NMS:%.1fms | #%d",
        fps, e2e_ms, capture_ms, npu_ms, nms_ms, frame_id);

    cv::Point pos(8, 18);
    // 黑色描边 (让文字在明/暗背景上都可读)
    cv::putText(bgr, buf, pos,
                cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(0, 0, 0), 3);
    // 白字
    cv::putText(bgr, buf, pos,
                cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(255, 255, 255), 1);
}

// ── BGR → NV12 → 写 FIFO 管道 ──
// 返回 true=写入成功, false=管道未打开
inline bool write_nv12_pipe(const cv::Mat& bgr, FILE* pipe) {
    if (!pipe) return false;
    cv::Mat i420;
    cv::cvtColor(bgr, i420, cv::COLOR_BGR2YUV_I420);
    int sz = bgr.cols * bgr.rows;  // 640*640
    // Y 平面 (全分辨率)
    fwrite(i420.data, 1, sz, pipe);
    // UV 交错 (I420 → NV12: U和V逐对交叉)
    for (int i = 0; i < sz / 4; i++) {
        uint8_t uv[2] = { i420.data[sz + i], i420.data[sz + sz / 4 + i] };
        fwrite(uv, 1, 2, pipe);
    }
    return true;
}
