from rknn.api import RKNN
import json

rknn = RKNN(verbose=True)
rknn.load_rknn("/home/alin/Code/my_project/yolo26/convert_cpp/weights/yolov5.rknn")  # 改成你的路径

# 模型加载后, input/output tensor 信息存在内部
try:
    print("=== 模型版本 ===")
    print(rknn.get_sdk_version())
except:
    pass

# 最简单的办法: 查看 rknn 对象的属性
for attr in dir(rknn):
    if any(k in attr.lower() for k in ['input', 'output', 'tensor', 'model']):
        print(f"  {attr}")
