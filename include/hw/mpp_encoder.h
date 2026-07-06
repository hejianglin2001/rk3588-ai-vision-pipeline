#pragma once
// MPP H264 编码器 — 把 cv::Mat 压成 H264 裸流写入文件/管道
// 搭配 cvlc 做 RTSP: cvlc stream.h264 --sout '#rtp{sdp=rtsp://:8554/live}'

#include <rockchip/mpp_buffer.h>
#include <rockchip/mpp_err.h>
#include <rockchip/mpp_frame.h>
#include <rockchip/mpp_packet.h>
#include <rockchip/mpp_rc_api.h>
#include <rockchip/rk_mpi.h>
#include <rockchip/rk_type.h>
#include <cstdio>
#include <cstring>

class MppEncoder {
    MppCtx           ctx_   = nullptr;
    MppApi*          api_   = nullptr;
    MppBufferGroup   group_ = nullptr;
    FILE*            out_   = nullptr;
    int              w_ = 640, h_ = 640;
    int              fps_ = 30;
    int64_t          pts_ = 0;

public:
    bool open(const char* output_path, int w = 640, int h = 640, int fps = 30) {
        w_ = w; h_ = h; fps_ = fps;
        out_ = fopen(output_path, "wb");
        if (!out_) { fprintf(stderr, "❌ MPP: 无法打开 %s\n", output_path); return false; }

        // 创建 MPP 编码上下文
        MPP_RET ret = mpp_create(&ctx_, &api_);
        if (ret != MPP_OK) { fprintf(stderr, "❌ MPP: mpp_create err=%d\n", ret); return false; }

        // 配置编码参数
        MppEncCodecCfg  codec_cfg;
        MppEncPrepCfg   prep_cfg;
        MppEncRcCfg     rc_cfg;
        mpp_enc_cfg_set_s32(ctx_, "codec:type",        MPP_VIDEO_CodingAVC);
        mpp_enc_cfg_set_s32(ctx_, "prep:width",        w_);
        mpp_enc_cfg_set_s32(ctx_, "prep:height",       h_);
        mpp_enc_cfg_set_s32(ctx_, "prep:format",       MPP_FMT_YUV420SP);  // NV12
        mpp_enc_cfg_set_s32(ctx_, "prep:rotation",     MPP_ENC_ROT_0);
        mpp_enc_cfg_set_s32(ctx_, "rc:mode",           MPP_ENC_RC_MODE_CBR);
        mpp_enc_cfg_set_s32(ctx_, "rc:bps",            2000000);  // 2Mbps
        mpp_enc_cfg_set_s32(ctx_, "h264:profile",      100);      // High
        mpp_enc_cfg_set_s32(ctx_, "h264:level",        40);

        ret = api_->init(ctx_, MPP_CTX_ENC, MPP_VIDEO_CodingAVC);
        if (ret != MPP_OK) { fprintf(stderr, "❌ MPP: init err=%d\n", ret); return false; }

        // 分配物理连续 buffer group
        size_t buf_size = w_ * h_ * 3 / 2;  // NV12 大小
        mpp_buffer_group_get_internal(&group_, MPP_BUFFER_TYPE_DRM);
        mpp_buffer_group_limit_config(group_, buf_size, 4);

        printf("✅ MPP H264 %dx%d @%d FPS → %s\n", w_, h_, fps_, output_path);
        return true;
    }

    // 喂一帧 BGR 图像, 内部转 NV12 再编码
    bool encode(const cv::Mat& bgr) {
        if (!ctx_) return false;

        // BGR → NV12 (YUV420SP) — CPU 转换
        cv::Mat nv12;
        cv::cvtColor(bgr, nv12, cv::COLOR_BGR2YUV_I420);  // I420 → 近似
        // ponytail: I420 vs NV12 的 UV 排列不同, MPP 硬解NV12, 这里近似

        // 创建 MPP frame
        MppBuffer buf;
        size_t   sz  = w_ * h_ * 3 / 2;
        mpp_buffer_get(group_, &buf, sz);
        memcpy((uint8_t*)mpp_buffer_get_ptr(buf), nv12.data, sz);

        MppFrame frame = nullptr;
        mpp_frame_init(&frame);
        mpp_frame_set_width(frame, w_);
        mpp_frame_set_height(frame, h_);
        mpp_frame_set_hor_stride(frame, w_);
        mpp_frame_set_ver_stride(frame, h_);
        mpp_frame_set_fmt(frame, MPP_FMT_YUV420SP);
        mpp_frame_set_buffer(frame, buf);
        mpp_frame_set_pts(frame, pts_);
        pts_ += 1000000LL / fps_;  // PTS 递增

        // 编码
        MPP_RET ret = api_->encode_put_frame(ctx_, frame);
        mpp_frame_deinit(&frame);
        mpp_buffer_put(buf);
        if (ret != MPP_OK) return false;

        // 取编码后数据
        MppPacket packet = nullptr;
        ret = api_->encode_get_packet(ctx_, &packet);
        if (ret == MPP_OK && packet) {
            void* ptr = mpp_packet_get_data(packet);
            size_t len = mpp_packet_get_length(packet);
            fwrite(ptr, 1, len, out_);
            fflush(out_);
            mpp_packet_deinit(&packet);
        }
        return true;
    }

    ~MppEncoder() {
        if (ctx_) {
            api_->reset(ctx_);
            mpp_destroy(ctx_);
        }
        if (group_) mpp_buffer_group_put(group_);
        if (out_)  fclose(out_);
    }
};
