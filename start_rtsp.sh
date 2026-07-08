#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
# RTSP 推流一键启动脚本
# 用法: ./start_rtsp.sh [模型路径]
#
# 生产链路 (3进程):
#   [1] C++ main → /tmp/video_pipe (NV12 裸流)
#   [2] GStreamer: videoparse → mpph264enc → h264parse
#       → tcpserversink (127.0.0.1:5601)     ← MPP 硬件编码
#   [3] ffmpeg: -f h264 -i tcp://5601 -c copy
#       → RTSP 推流到 mediamtx (:8554)
#   [4] mediamtx: RTSP 服务端 (端口 8554)
#       ← 客户端连接: rtsp://板子IP:8554/live
#
# 为什么这样设计:
#   - MPP 硬件编码: RK3588 VPU, 不占 CPU
#   - mediamtx: 企业级 RTSP 服务端 (GitHub 25k+ star)
#   - ffmpeg 中转: 只做协议转换 (H264 TCP → RTSP), -c copy 不重编
#   - tcpserversink: GStreamer → ffmpeg 用本地 TCP, 比 UDP 可靠
# ═══════════════════════════════════════════════════════════════════
#convert_cpp/weights/yolo26n_split_16.rknn yolo26n_split.rknn convert_cpp/weights/yolo26n_fp16.rknn
MODEL="${1:-./weights/yolo26n_split.rknn}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# tools/ 位置: 优先找项目根目录, 其次脚本同目录
PARENT_DIR="$(dirname "$SCRIPT_DIR")"
TOOLS_DIR="$PARENT_DIR/tools"
[ -d "$TOOLS_DIR" ] || TOOLS_DIR="$SCRIPT_DIR/tools"

MEDIAMTX="$TOOLS_DIR/mediamtx"
CONFIG="$TOOLS_DIR/mediamtx.yml"

# ── 0. 环境检查 ──
if [ ! -f "$MEDIAMTX" ]; then
    echo "❌ mediamtx 不存在: $MEDIAMTX"
    echo ""
    echo "   下载方法:"
    echo "   mkdir -p $TOOLS_DIR && cd $TOOLS_DIR"
    echo "   wget https://github.com/bluenviron/mediamtx/releases/download/v1.11.3/mediamtx_v1.11.3_linux_arm64v8.tar.gz"
    echo "   tar xzf mediamtx_v*.tar.gz && rm mediamtx_v*.tar.gz"
    exit 1
fi

# ── 1. 清理所有旧进程和端口 ──
echo "🧹 清理旧进程..."
for port in 5601 8554 8000; do
    kill -9 $(lsof -t -i:$port) 2>/dev/null
done
pkill -9 -f mediamtx    2>/dev/null
pkill -9 -f ffmpeg       2>/dev/null
pkill -9 -f gst-launch   2>/dev/null
sleep 0.5

# 确认端口释放
if ss -tlnp 2>/dev/null | grep -qE "5601|8554"; then
    echo "⚠️  端口未释放, 再等一下..."
    sleep 1
fi

# ── 2. 创建 FIFO ──
[ -p /tmp/video_pipe ] || mkfifo /tmp/video_pipe
echo "📁 FIFO: /tmp/video_pipe"

# ── 3. 写 mediamtx 配置 ──
cat > "$CONFIG" << 'EOF'
rtspAddress: :8554
rtspTransports: [udp, tcp]
paths:
  live:
    source: publisher
EOF

# ── 4. 启动 mediamtx (RTSP 服务端) ──
echo "🎥 启动 RTSP 服务端 (端口 8554)..."
"$MEDIAMTX" "$CONFIG" &
sleep 1.5

# ── 5. 启动 GStreamer (MPP 硬编码 → H264 TCP) ──
echo "🔧 启动 MPP 硬编码管线..."
gst-launch-1.0 filesrc location=/tmp/video_pipe ! \
    videoparse format=nv12 width=640 height=640 framerate=30/1 ! \
    mpph264enc ! h264parse ! \
    queue max-size-buffers=1 max-size-time=0 max-size-bytes=0 ! \
    tcpserversink host=127.0.0.1 port=5601 &
sleep 0.5

# ── 6. 启动 ffmpeg (H264 TCP → RTSP) ──
echo "📡 推流到 mediamtx (低延迟)..."
nohup ffmpeg -fflags nobuffer -flags low_delay \
    -f h264 -probesize 32 -i tcp://127.0.0.1:5601 \
    -c copy -f rtsp rtsp://127.0.0.1:8554/live </dev/null &
sleep 0.5

# ── 7. 启动推理 ──
echo "🚀 启动推理..."
if [ -d "$SCRIPT_DIR/build" ]; then
    cd "$SCRIPT_DIR/build"
fi
./yolo_inference "$MODEL"

echo ""
echo "✅ RTSP 推流已就绪"
echo "   ffplay:  ffplay -fflags nobuffer -flags low_delay rtsp://$(hostname -I | awk '{print $1}'):8554/live"
echo ""

./yolo_inference "$MODEL"
