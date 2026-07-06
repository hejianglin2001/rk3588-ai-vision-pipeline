#pragma once
// RGA 硬件 resize + 颜色转换 (已验证: posix_memalign + mlock + imresize)
// 用法:
//   cv::Mat dst(640, 640, CV_8UC3);
//   int ok = rga_uyvy_to_rgb(src_ptr, 1920, 1080, dst);

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include "im2d.h"
#include "rga.h"

// ── UYVY(1920x1080) → RGB(640x640) 硬件一步完成 ──
// src: 摄像头原始 UYVY 数据, w×h 是摄像头分辨率
// dst: OpenCV Mat (必须已分配 640×640 CV_8UC3)
// 返回 0=成功, -1=失败
inline int rga_uyvy_to_rgb(const void* src, int src_w, int src_h,
                           const cv::Mat& dst) {
    int src_stride = ((src_w * 2) + 15) & ~15;   // UYVY: 2 bytes/px, 16对齐
    int dst_stride = ((dst.cols * 3) + 15) & ~15; // RGB:  3 bytes/px, 16对齐

    mlock(src, src_stride * src_h);               // 锁定源页 (V4L2 DMA buf)
    mlock(dst.data, dst_stride * dst.rows);       // 锁定目标页

    rga_buffer_t rga_src = wrapbuffer_virtualaddr(
        (void*)src, src_w, src_h, RK_FORMAT_UYVY_422);
    rga_buffer_t rga_dst = wrapbuffer_virtualaddr(
        (void*)dst.data, dst.cols, dst.rows, RK_FORMAT_RGB_888);

    IM_STATUS ret = imresize(rga_src, rga_dst);

    munlock(src, src_stride * src_h);
    munlock(dst.data, dst_stride * dst.rows);

    if (ret != IM_STATUS_SUCCESS) {
        fprintf(stderr, "❌ [RGA] imresize err=%d\n", ret);
        return -1;
    }
    return 0;
}
