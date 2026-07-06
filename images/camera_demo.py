import cv2
import time
import numpy as np
 
 
def main():
    # 使用GStreamer管道从MIPI摄像头捕获视频，添加视频帧率
    # cap = cv2.VideoCapture('/dev/video21')  # USB摄像头
    cap = cv2.VideoCapture('/dev/video11', cv2.CAP_ANY)  # MIPI摄像头
    cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'NV12'))
    frames, loopTime, initTime = 0, time.time(), time.time()
    fps = 0
    while True:
        frames += 1
        # 从摄像头捕获帧
        ret, frame = cap.read()
        # 如果捕获到帧，则显示它
        if ret:
            if frames % 30 == 0:
                print("30帧平均帧率:\t", 30 / (time.time() - loopTime), "帧")
                fps = 30 / (time.time() - loopTime)
                loopTime = time.time()
            frame = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
            cv2.putText(frame, "FPS: {:.2f}".format(fps), (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 0, 255),
                        2)  # 在图像上显示帧率
            cv2.imshow("MIPI Camera", frame)
        # 按下'q'键退出循环
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break
 
    print("总平均帧率\t", frames / (time.time() - initTime))
    # 释放资源并关闭窗口
    cap.release()
    cv2.destroyAllWindows()
 
 
if __name__ == "__main__":
    main()