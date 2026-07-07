#pragma once
// ╔══════════════════════════════════════════════════════════╗
// ║  画面叠加工具 — 检测框 + 性能指标 + NV12 推流          ║
// ║  从 main.cpp 抽离, 保持主循环可读                      ║
// ╚══════════════════════════════════════════════════════════╝

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
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
        // 黑色描边提高可读性
        cv::putText(bgr, label, cv::Point(b.x1, b.y1 - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 0, 0), 3);
        cv::putText(bgr, label, cv::Point(b.x1, b.y1 - 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
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

// ── BGR → NV12 → 写 FIFO (poll检查, 满则丢帧) ──
inline bool write_nv12_pipe(const cv::Mat& bgr, FILE* pipe) {
    if (!pipe) return false;
    int fd = fileno(pipe);
    int total = bgr.cols * bgr.rows * 3 / 2;  // NV12 size
    // 检查管道是否可写, 不可写则丢帧
    struct pollfd pfd = {fd, POLLOUT, 0};
    if (poll(&pfd, 1, 0) <= 0) return false;  // 管道满, 跳过

    cv::Mat i420;
    cv::cvtColor(bgr, i420, cv::COLOR_BGR2YUV_I420);
    int sz = bgr.cols * bgr.rows;
    static std::vector<uint8_t> nv12(sz + sz/2);
    memcpy(nv12.data(), i420.data, sz);
    int uv_sz = sz / 4;
    uint8_t* dst_uv = nv12.data() + sz;
    for (int i = 0; i < uv_sz; ++i) {
        dst_uv[i*2]   = i420.data[sz + i];
        dst_uv[i*2+1] = i420.data[sz + uv_sz + i];
    }
    // poll过了, 管道有空间, write 不会阻塞
    write(fd, nv12.data(), total);
    return true;
}
