## 从视频中提取初始位置的小视频
从学生全景视频中，跳到指定视频位置，通过 yolov5 定位每个学生的初始位置，为 N 个位置开始提取图像，并压缩为 320x240 的小视频，直到结束

### 使用方式
./zk_build_student_vid <student-full.mp4> -f <from> -t <to> -N <N>