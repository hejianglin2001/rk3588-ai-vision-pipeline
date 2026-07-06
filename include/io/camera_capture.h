#pragma once
// ╔══════════════════════════════════════════════════════════╗
// ║  摄像头采集类 — 用 V4L2 API 直读 MIPI 摄像头            ║
// ║  不依赖 OpenCV VideoCapture, 零额外封装开销              ║
// ╚══════════════════════════════════════════════════════════╝

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <cstdio>
#include <fcntl.h>       // open() — 打开文件/设备的系统调用
#include <unistd.h>      // close()
#include <sys/ioctl.h>   // ioctl() — 和硬件驱动"说话"的接口
#include <sys/mman.h>    // mmap() — 把内核内存映射到用户空间
#include <linux/videodev2.h>  // V4L2 结构体定义

class CameraCapture {
    // ── private (默认) — 外部不能直接访问, 只能通过 public 方法 ──
    //    命名约定: 成员变量加 _ 后缀, 一眼看出不是局部变量
    int      fd_    = -1;    // 文件描述符 (Linux 一切皆文件, 摄像头也是)
    int      width_ = 640;   // 当前分辨率 (默认 640)
    int      height_= 640;
    uint32_t pixfmt_ = 0;    // 像素格式 (如 UYVY), 0=未初始化
    int      dma_fd_ = -1;   // DMA-BUF 文件描述符 (给 RGA 硬件加速用)
    int      qbuf_idx_ = -1;  // 待归还的 buffer index (-1=无)
    struct Buf { void* p; size_t len; } buffers_[6];  // 6 个内核缓冲区

public:
    // ── 打开摄像头 ──
    // const char*: "指向只读字符串的指针" — 传进来的路径不会被修改
    // int w = 640: 默认参数, 不传就用 640
    bool open(const char* device, int w = 640, int h = 640) {
        // ::open — 前面加 :: 表示调"全局命名空间"的 open
        // 不加 :: 可能跟类的方法名冲突
        fd_ = ::open(device, O_RDWR);  // O_RDWR = 读写模式
        if (fd_ < 0) { perror("open"); return false; }

        // = {} — 结构体初始化列表, 把所有字段设为零
        struct v4l2_format fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;  // Multiplanar: 多平面存储
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_UYVY;  // 强制 UYVY (IMX415)
        fmt.fmt.pix_mp.width       = (__u32)w;            // __u32 = unsigned 32bit
        fmt.fmt.pix_mp.height      = (__u32)h;
        // ioctl(fd, 命令, 参数指针) — 给硬件发命令
        ioctl(fd_, VIDIOC_S_FMT, &fmt);  // 设置格式
        ioctl(fd_, VIDIOC_G_FMT, &fmt);  // 回读确认 (硬件可能改了我们的请求)
        pixfmt_ = fmt.fmt.pix_mp.pixelformat;
        width_  = fmt.fmt.pix_mp.width;
        height_ = fmt.fmt.pix_mp.height;
        // 打印格式 (把 4 字节 code 拆成 4 个字符)
        printf("📷 %dx%d %c%c%c%c\n", width_, height_,
               pixfmt_&0xFF,(pixfmt_>>8)&0xFF,(pixfmt_>>16)&0xFF,(pixfmt_>>24)&0xFF);

        // 申请 6 个内核缓冲区 (DMA 用, 物理连续)
        struct v4l2_requestbuffers req = {};
        req.count = 6; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        req.memory = V4L2_MEMORY_MMAP;  // MMAP = 映射到用户空间
        if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) { perror("REQBUFS"); return false; }

        // mmap() — 把每个内核缓冲区"映射"到用户空间
        // 之后直接读写 buffers_[i].p 就是在操作内核 DMA 内存
        for (int i = 0; i < 6; i++) {
            struct v4l2_buffer buf = {};
            struct v4l2_plane planes[1] = {};  // 单平面 (UYVY 是 packed 格式)
            buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory   = V4L2_MEMORY_MMAP;
            buf.index    = (__u32)i;
            buf.m.planes = planes;  // 指向平面数组
            buf.length   = 1;       // 1 个平面
            ioctl(fd_, VIDIOC_QUERYBUF, &buf);           // 查询缓冲区信息
            buffers_[i].len  = buf.m.planes[0].length;   // 记录大小
            buffers_[i].p    = mmap(                     // 映射!
                NULL,                                    // 内核选地址
                buf.m.planes[0].length,                  // 映射大小
                PROT_READ|PROT_WRITE,                    // 可读可写
                MAP_SHARED,                              // 共享 (CPU和DMA都能看到)
                fd_,
                buf.m.planes[0].m.mem_offset             // 物理地址偏移
            );
        }
        // QBUF — 把空 buffer 还给驱动队列 (驱动往里填数据)
        for (int i = 0; i < 6; i++) {
            struct v4l2_buffer buf = {};
            struct v4l2_plane planes[1] = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_MMAP; buf.index = (__u32)i;
            buf.m.planes = planes; buf.length = 1;
            ioctl(fd_, VIDIOC_QBUF, &buf);
        }
        // STREAMON — 启动摄像头数据流
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        ioctl(fd_, VIDIOC_STREAMON, &type);
        printf("📷 [V4L2] 流已开启\n");
        return true;
    }

    // ── 读一帧 (UYVY → BGR 转换) ──
    // cv::Mat& bgr — & 是"引用", 相当于别名, 不拷贝整个 Mat
    //   调用者传进来的 bgr 会被直接修改 (输出参数)
    // do_qbuf=true: 读完立即归还 (CPU模式)
    // do_qbuf=false: 延迟归还, 外部调 qbuf() (RGA DMA-BUF 模式)
    bool read(cv::Mat& bgr, bool do_qbuf = true) {
        struct v4l2_buffer buf = {};
        struct v4l2_plane planes[1] = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.m.planes = planes; buf.length = 1;
        if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) return false;

        struct v4l2_exportbuffer exp = {};
        exp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        exp.index = buf.index; exp.plane = 0;
        ioctl(fd_, VIDIOC_EXPBUF, &exp);
        dma_fd_ = exp.fd;

        uint8_t* data = (uint8_t*)buffers_[buf.index].p;
        if (pixfmt_ == V4L2_PIX_FMT_NV12 || pixfmt_ == V4L2_PIX_FMT_NV21) {
            cv::Mat yuv(height_+height_/2, width_, CV_8UC1, data);
            cv::cvtColor(yuv, bgr,
                pixfmt_==V4L2_PIX_FMT_NV12?cv::COLOR_YUV2BGR_NV12:cv::COLOR_YUV2BGR_NV21);
        } else if (pixfmt_ == V4L2_PIX_FMT_UYVY) {
            cv::Mat uyvy(height_, width_, CV_8UC2, data);
            cv::cvtColor(uyvy, bgr, cv::COLOR_YUV2BGR_UYVY);
        } else {
            cv::Mat gray(height_, width_, CV_8UC1, data);
            cv::cvtColor(gray, bgr, cv::COLOR_GRAY2BGR);
        }

        if (do_qbuf) {
            ioctl(fd_, VIDIOC_QBUF, &buf);
        } else {
            qbuf_idx_ = buf.index;  // 记住 index, 等 qbuf() 归还
        }
        return true;
    }

    // ── 读原始帧 (不做颜色转换, 给 RGA 用) ──
    // 返回 data 指针直接指向内核 DMA 缓冲区, RGA 零拷贝读
    // 调用后必须手动 qbuf() 归还 buffer
    bool read_raw(uint8_t*& data, uint32_t& fmt) {
        struct v4l2_buffer buf = {};
        struct v4l2_plane planes[1] = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.m.planes = planes; buf.length = 1;
        if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) return false;

        // 导出 DMA-BUF fd (给 RGA fd 模式备用)
        struct v4l2_exportbuffer exp = {};
        exp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        exp.index = buf.index; exp.plane = 0;
        ioctl(fd_, VIDIOC_EXPBUF, &exp);
        dma_fd_ = exp.fd;

        data = (uint8_t*)buffers_[buf.index].p;
        fmt  = pixfmt_;
        qbuf_idx_ = buf.index;  // 记住, 等 RGA 用完归还
        return true;
    }

    // 归还延迟的 buffer (RGA 用完后调)
    void qbuf() {
        if (qbuf_idx_ < 0) return;
        struct v4l2_buffer buf = {};
        struct v4l2_plane planes[1] = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = (__u32)qbuf_idx_;
        buf.m.planes = planes; buf.length = 1;
        ioctl(fd_, VIDIOC_QBUF, &buf);
        qbuf_idx_ = -1;
    }

    // ── Getter 方法 ──
    // 函数后面的 const 表示 "这个方法不会修改任何成员变量"
    // 调用方拿到 const 引用时可以放心调
    int      get_fd()      const { return dma_fd_; }
    int      get_width()   const { return width_; }
    int      get_height()  const { return height_; }
    uint32_t get_pixfmt()  const { return pixfmt_; }

    // ── 析构函数 (名字前有 ~) ──
    // 对象销毁时自动调用, 负责清理资源 (关设备, 释放内存)
    ~CameraCapture() {
        if (fd_ >= 0) {
            int t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            ioctl(fd_, VIDIOC_STREAMOFF, &t);        // 停数据流
            // auto& — 自动类型推导 + 引用 (遍历数组不拷贝)
            for (auto& b : buffers_) if (b.p) munmap(b.p, b.len);  // 解除映射
            ::close(fd_);  // 关设备
        }
    }
};
