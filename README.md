为了方便截取视频中多个指定位置的视频片段，编写了该工具，读入 act_box.txt 文件

    x1 y1 x2 y2 score cls\n

文件中每行一个矩形框，x1,y1 为左上角的像素坐标，x2,y2 为右下角坐标，该文件一般用行为检测模型得到图像中每个人的坐标生成；

使用 avfilter 的 buffer, split, crop, scale, buffersink 实现视频中的行为框扣图，每个 bbox 对应位置的扣图压缩为 320x240 的 h264 视频存储；
