#ifndef _input_media_hh
#define _input_media_hh

extern "C" {
#   include <libavcodec/avcodec.h>
#   include <libavformat/avformat.h>
#   include <libswscale/swscale.h>
#   include <libavutil/avutil.h>
#   include <libavfilter/avfilter.h>
}

#include <vector>
#include <string>

/// 视频解码
class VideoDec {
    AVFormatContext *__fc = nullptr;
    AVCodecContext *__cc = nullptr;

    std::string __fname;
    double __duration = -1.0;
    int __sid = -1; // stream id

    AVPacket *__pkt = nullptr;
    AVFrame *__frame = nullptr;

public:
    // 打开输入视频文件
    int open(const char *fname);
    int close();

    double get_duration();
    int seek(double pos);
    
    // 返回: > 0 得到 frame, == 0 EOF, < 0 失败
    int get_frame(double *stamp, AVFrame **frame);

private:
    int __try_get_frame(double *stamp, AVFrame **frame);
};


class Box {
public:
    int x1, y1, x2, y2;
    const char *title;
    double score;
};

/// 图像多路 crop resize 到 (w, h)
class FrameCrop {
    std::vector<Box> __boxes;
    AVFilterGraph *__graph = nullptr;

    std::vector<AVFilterContext*> __filters;
    
    AVFilterContext *__src;
    std::vector<AVFilterContext*> __sinks;
public:
    int open(int w, int h, AVPixelFormat fmt, const std::vector<Box> &boxes, 
            int target_width, int target_height);    // 
    int close();

    int put(AVFrame *frame);
    std::vector<AVFrame*> get();
};

/// 视频编码
class VideoEnc {
    AVFormatContext *__fc = nullptr;
    AVCodecContext *__cc = nullptr;

    double __stamp_off = -1.0;

public:
    int open(const char *fname, int width, int height, int fps=25, int bitrate=50000);
    int close();

    int put_frame(double stamp, AVFrame *frame);
};

#endif // 