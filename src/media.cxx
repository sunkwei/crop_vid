#include "media.hxx"
extern "C" {
#   include <libavfilter/buffersrc.h>
#   include <libavfilter/buffersink.h>
#   include <libavutil/opt.h>
}

struct Init {
    Init() {
        avformat_network_init();
        av_log_set_level(AV_LOG_FATAL);
    }
    ~Init() {
        avformat_network_deinit();
    }
};

static Init _init;


//////////////////////// dec
int VideoDec::open(const char *fname) {
    __fname = fname;
    int rc = avformat_open_input(&__fc, fname, 0, 0);
    if (rc < 0) {
        fprintf(stderr, "ERR: %s:%s cannot open fname:%s\n", __func__, __LINE__, fname);
        return -1;
    }

    rc = avformat_find_stream_info(__fc, 0);

    av_dump_format(__fc, -1, fname, 0);

    for (int i = 0; i < __fc->nb_streams; i++) {
        AVStream *stream = __fc->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            const AVCodec *c = avcodec_find_decoder(stream->codecpar->codec_id);
            if (c) {
                __cc = avcodec_alloc_context3(c);
                avcodec_parameters_to_context(__cc, stream->codecpar);
                avcodec_open2(__cc, c, 0);

                if (__fc->pb->seekable) {
                    __duration = 1.0 * stream->duration * stream->time_base.num / stream->time_base.den;
                }

                __sid = i;
                break;
            }
        }
    }

    if (!__cc) {
        fprintf(stderr, "ERR: %s:%s cannot find video stream from %s\n", __func__, __LINE__, fname);
        avformat_close_input(&__fc);
        return -1;
    }

    __pkt = av_packet_alloc();
    __frame = av_frame_alloc();

    return 0;
}

int VideoDec::close() {
    if (__cc) {
        avcodec_close(__cc);
        avcodec_free_context(&__cc);
    }
    if (__fc) {
        avformat_close_input(&__fc);
    }

    av_packet_free(&__pkt);
    av_frame_free(&__frame);

    return 0;
}

double VideoDec::get_duration() {
    return __duration;
}

int VideoDec::seek(double pos) {
    if (__fc) {
        auto &ts = __fc->streams[__sid]->time_base;
        int64_t stamp = (int64_t)(pos * ts.den / ts.num);
        return av_seek_frame(__fc, __sid, stamp, AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD);
    }
    return -1;
}

int VideoDec::__try_get_frame(double *pos, AVFrame **pic) {
    *pic = nullptr;
    int rc = 0;
    if (!__fc) return -1;

    // 上次是否有遗留
    rc = avcodec_receive_frame(__cc, __frame);
    if (rc == 0) {
        *pic = __frame;
        return 1;
    }

    rc = av_read_frame(__fc, __pkt);
    if (rc < 0)
        return rc;

    if (__pkt->stream_index != __sid) {
        av_packet_unref(__pkt);
        return 0;
    }

    // FIXME: 使用 frame 中，还是 packet 中的时间戳？
    *pos = 1.0 * __pkt->pts * __fc->streams[__sid]->time_base.num / __fc->streams[__sid]->time_base.den;
    
    rc = avcodec_send_packet(__cc, __pkt);
    av_packet_unref(__pkt);

    rc = avcodec_receive_frame(__cc, __frame);
    if (rc == 0) {
        *pic = __frame;
        return 1;
    }
    if (AVERROR(EAGAIN) == rc) {
        return 0;   // 重试
    }
    return rc;
}

int VideoDec::get_frame(double *pos, AVFrame **pic) {
    int rc = __try_get_frame(pos, pic);
    while (rc == 0)
        rc = __try_get_frame(pos, pic);
    return rc;
}

////////////////// crop
// source -> split -> crop -> scale -> sink
//               |
//               ---> crop -> scale -> sink 
int FrameCrop::open(int w, int h, AVPixelFormat fmt, const std::vector<Box> &boxes, int cw, int ch) {
    char buf[128];
    __graph = avfilter_graph_alloc();

    AVFilterContext *ctx_buffer;
    snprintf(buf, sizeof(buf), "width=%d:height=%d:pix_fmt=yuv420p:time_base=1/25", w, h, "yuv420p");
    int rc = avfilter_graph_create_filter(&ctx_buffer, avfilter_get_by_name("buffer"),
            "source", buf, 0, __graph);
    if (rc < 0) {
        fprintf(stderr, "ERR: %s:%d cannot create buffer source filter!\n", __func__, __LINE__);
        return -1;
    }
    __filters.push_back(ctx_buffer);
    __src = ctx_buffer;
    
    AVFilterContext *ctx_split;
    snprintf(buf, sizeof(buf), "%d", (int)boxes.size());
    rc = avfilter_graph_create_filter(&ctx_split, avfilter_get_by_name("split"),
            "split", buf, 0, __graph);
    if (rc < 0) {
        fprintf(stderr, "ERR: %s:%d cannot create split filter!\n", __func__, __LINE__);
        return -1;
    }
    __filters.push_back(ctx_split);

    rc = avfilter_link(ctx_buffer, 0, ctx_split, 0);

    char name[64];
    for (auto i = 0; i < boxes.size(); i++) {
        AVFilterContext *ctx_crop;
        const auto &box = boxes[i];
        snprintf(buf, sizeof(buf), "x=%d:y=%d:w=%d:h=%d", box.x1, box.y1, box.x2 - box.x1, box.y2 - box.y1);
        snprintf(name, sizeof(name), "crop_%d", i);
        rc = avfilter_graph_create_filter(&ctx_crop, avfilter_get_by_name("crop"), name, buf, 0, __graph);
        __filters.push_back(ctx_crop);
        rc = avfilter_link(ctx_split, i, ctx_crop, 0);

        AVFilterContext *ctx_scale;
        snprintf(buf, sizeof(buf), "w=%d:h=%d", cw, ch);
        snprintf(name, sizeof(name), "scale_%d", i);
        rc = avfilter_graph_create_filter(&ctx_scale, avfilter_get_by_name("scale"), name, buf, 0, __graph);
        __filters.push_back(ctx_scale);
        rc = avfilter_link(ctx_crop, 0, ctx_scale, 0);

        AVFilterContext *ctx_sink;
        snprintf(name, sizeof(name), "sink_%d", i);
        rc = avfilter_graph_create_filter(&ctx_sink, avfilter_get_by_name("buffersink"), name, 0, 0, __graph);
        __filters.push_back(ctx_sink);
        __sinks.push_back(ctx_sink);
        rc = avfilter_link(ctx_scale, 0, ctx_sink, 0);
    }

    rc = avfilter_graph_config(__graph, 0);
    if (rc < 0) {
        fprintf(stderr, "ERR: %s:%d avfilter_graph_config err, rc=%d\n", __func__, __LINE__, rc);
        return -1;
    }

    // char *dumps = avfilter_graph_dump(__graph, 0);
    // fprintf(stdout, "%s\n", dumps);
    // av_free(dumps);

    return 0;
}

int FrameCrop::close() {
    avfilter_graph_free(&__graph);
    // for (auto filter: __filters) {
    //     avfilter_free(filter);
    // }
    __filters.clear();
    return 0;
}

int FrameCrop::put(AVFrame *frame) {
    return av_buffersrc_add_frame(__src, frame);
}

std::vector<AVFrame *> FrameCrop::get() {
    std::vector<AVFrame *> frames;
    for (auto &sink: __sinks) {
        AVFrame *frame = av_frame_alloc();
        int rc = av_buffersink_get_frame(sink, frame);
        if (rc >= 0)
            frames.push_back(frame);
        else {
            av_frame_free(&frame);
            frames.push_back(0);
        }
    }
    return frames;
}

////////////////// enc
int VideoEnc::open(const char *fname, int width, int height, int fps, int bitrate) {
    const AVOutputFormat *fmt = av_guess_format(NULL, fname, NULL);
    int rc = avformat_alloc_output_context2(&__fc, fmt, NULL, fname);
    if (rc < 0) {
        fprintf(stderr, "ERR: %s:%d cannot create output avformat!!!\n", __func__, __LINE__);
        return -1;
    }

    const AVCodec *c = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!c) {
        fprintf(stderr, "ERR: %s:%d not encoder for h264!\n", __func__, __LINE__);
        return -1;
    }

    AVStream *stream = avformat_new_stream(__fc, c);
    if (!stream) {
        fprintf(stderr, "ERR: %s:%d cannot create new stream!\n", __func__, __LINE__);
        return -1;
    }

    __cc = avcodec_alloc_context3(c);
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id = c->id;
    stream->codecpar->width = width;
    stream->codecpar->height = height;
    stream->codecpar->format = AV_PIX_FMT_YUV420P;
    stream->codecpar->bit_rate = bitrate;
    avcodec_parameters_to_context(__cc, stream->codecpar);
    __cc->time_base = (AVRational){ 1, 90000 };
    __cc->max_b_frames = 0;
    __cc->gop_size = fps;
    __cc->framerate = (AVRational){ fps, 1};
    av_opt_set(__cc, "preset", "ultrafast", 0);
    avcodec_parameters_from_context(stream->codecpar, __cc);

    rc = avcodec_open2(__cc, c, 0);
    if (rc < 0) {
        fprintf(stderr, "ERR: %s:%d failed to open enc!!\n", __func__, __LINE__);
        return -1;
    }

    rc = avio_open(&__fc->pb, fname, AVIO_FLAG_WRITE);
    if (rc < 0) {
        fprintf(stderr, "ERR: %s:%d failed to open write file!\n", __func__, __LINE__);
        return -1;
    }

    rc = avformat_write_header(__fc, 0);
    if (rc < 0) {
        fprintf(stderr, "ERR: %s:%d failed to write head!!\n", __func__, __LINE__);
        return 0;
    }

    return 0;
}

int VideoEnc::close() {
    put_frame(0, 0);
    av_write_trailer(__fc);
    avio_close(__fc->pb);
    avformat_free_context(__fc);
    avcodec_close(__cc);
    return 0;
}

int VideoEnc::put_frame(double stamp, AVFrame *frame) {
    if (__stamp_off < 0) {
        __stamp_off = stamp;
    }

    stamp -= __stamp_off;

    if (frame) {
        frame->pts = (int64_t)(stamp * __cc->time_base.den / __cc->time_base.num);
        frame->time_base = __cc->time_base;
    }

    int rc = avcodec_send_frame(__cc, frame);
    if (rc < 0) {
        fprintf(stderr, "ERR: %s:%d enc send frame err!\n", __func__, __LINE__);
        return 0;
    }

    AVPacket *pkt = av_packet_alloc();
    pkt->data = nullptr;
    pkt->size = 0;
    pkt->flags |= AV_PKT_FLAG_KEY;
    rc = avcodec_receive_packet(__cc, pkt);
    if (rc == 0) {
        av_interleaved_write_frame(__fc, pkt);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    return 0;
}