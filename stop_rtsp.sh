#!/bin/bash
# 停止所有 RTSP 相关进程
echo "🧹 停止 RTSP 推流..."
for port in 5601 8554 8000; do
    kill -9 $(lsof -t -i:$port) 2>/dev/null
done
pkill -9 -f gst-launch   2>/dev/null
pkill -9 -f 'ffmpeg.*8554' 2>/dev/null
pkill -9 -f mediamtx     2>/dev/null
rm -f /tmp/video_pipe
echo "✅ 已停止"
