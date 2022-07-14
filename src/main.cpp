#include <stdio.h>
#include <string>
#include "media.hxx"
#include <chrono>
#include <thread>

using namespace std::chrono_literals;

struct Opts {
    std::string inp_fname;  // 输入视频文件名字
    const char *box_fname;  // 框描述文件
    double from;            // 起始时间戳，默认 60.0，希望跳过教室初期混乱
    double duration;        // 持续时间，默认 60.，整节课，秒
    int target_width, target_height; // 目标视频大小，默认 320 x 240
    int max_person_cnt;         // 最多人数，默认 10
    int debug;              // 是否输出更多信息 ...    
};

static Opts _opts;

static std::vector<Box> load_boxes_from_file(const char *fname) {
    // 文件每行一个 box，分别为 x1 y1 x2 y2\n
    FILE *fp = fopen(fname, "r");
    if (!fp) {
        fprintf(stderr, "ERR: %s:%d cannot open boxes file;%s\n", __func__, __LINE__, fname);
        return {};
    }

    std::vector<Box> boxes;
    while (!feof(fp)) {
        int x1, y1, x2, y2;
        int rc = fscanf(fp, "%d %d %d %d\n", &x1, &y1, &x2, &y2);
        if (rc == 4) {
            boxes.push_back({x1, y1, x2, y2});
        }
    }

    fclose(fp);

    return boxes;
}

static int parse_opts(Opts *opts, int argc, char **argv) {
    // app inp_fname -b box_fname -f from -d duration -w target_width -h target_height -N max_person_cnt -v
    opts->box_fname = 0;
    opts->from = 60.0;
    opts->duration = 60.0;
    opts->target_width = 320;
    opts->target_height = 240;
    opts->debug = 0;

    int curr = 0;
    while (++curr < argc) {
        if (strcmp(argv[curr], "-f") == 0) {
            if (curr + 1 < argc) {
                opts->from = atof(argv[curr+1]);
                curr += 1;
            }
            else {
                fprintf(stderr, "ERR: %s:%d no from value\n", __func__, __LINE__);
                return -1;
            }
        }
        else if (strcmp(argv[curr], "-d") == 0) {
            if (curr + 1 < argc) {
                opts->duration = atof(argv[curr+1]);
                curr += 1;
            }
            else {
                fprintf(stderr, "ERR: %s:%d no duration value\n", __func__, __LINE__);
                return -1;
            }
        }
        else if (strcmp(argv[curr], "-w") == 0) {
            if (curr + 1 < argc) {
                opts->target_width = atoi(argv[curr+1]);
                curr += 1;
            }
            else {
                fprintf(stderr, "ERR: %s:%d no target_wdith value\n", __func__, __LINE__);
                return -1;
            }
        }
        else if (strcmp(argv[curr], "-h") == 0) {
            if (curr + 1 < argc) {
                opts->target_height = atoi(argv[curr+1]);
                curr += 1;
            }
            else {
                fprintf(stderr, "ERR: %s:%d no target_height value\n", __func__, __LINE__);
                return -1;
            }
        }
        else if (strcmp(argv[curr], "-N") == 0) {
            if (curr + 1 < argc) {
                opts->max_person_cnt = atoi(argv[curr+1]);
                curr += 1;
            }
            else {
                fprintf(stderr, "ERR: %s:%d no max_person_cnt value\n", __func__, __LINE__);
                return -1;
            }
        }
        else if (strcmp(argv[curr], "-v") == 0) {
            opts->debug = 1;
        }
        else if (argv[curr][0] == '-') {
            fprintf(stderr, "ERR: %s:%d unknown param: %s\n", __func__, __LINE__, argv[curr]);
            return -1;
        }
        else {
            opts->inp_fname = argv[curr];
        }
    }

    if (opts->debug) {
        fprintf(stdout, "DEBUG: using opts\n");
        fprintf(stderr, "    inp fname: %s\n", opts->inp_fname.c_str());
        fprintf(stdout, "    boxes fnmae: %s\n", opts->box_fname);
        fprintf(stdout, "    from: %.03f\n", opts->from);
        fprintf(stderr, "    duration: %.01f\n", opts->duration);
        fprintf(stderr, "    target size: %d x %d\n", opts->target_width, opts->target_height);
        fprintf(stderr, "    max person cnt: %d\n", opts->max_person_cnt);
    }

    return 0;
}


int main(int argc, char **argv) {
    if (parse_opts(&_opts, argc, argv) < 0) {
        return -1;
    }
    if (_opts.inp_fname.empty()) {
        fprintf(stderr, "ERR: %s:%d NO inp video?\n", __func__, __LINE__);
        return -1;
    }

    if (!_opts.box_fname) {
        fprintf(stdout, "WARN: no boxes fname, using default: {300, 400, 700, 700}\n");
    }

    VideoDec input;
    int rc = input.open(_opts.inp_fname.c_str());
    if (rc < 0) {
        fprintf(stderr, "ERR: %s:%s cannot open input fname:%s\n", __func__, __LINE__, _opts.inp_fname.c_str());
        return -1;
    }

    double duration = input.get_duration();
    fprintf(stdout, "DEBUG: duration: %.03f seconds\n", duration);

    input.seek(_opts.from);

    double stamp;
    AVFrame *frame;

    // 第一帧，需要进行目标检测 ...
    rc = input.get_frame(&stamp, &frame);
    if (rc <= 0) {
        fprintf(stderr, "ERR: %s:%d no any valid picture!!!\n", __func__, __LINE__);
        return -1;
    }

    // TODO: 通过目标检测 ... 
    std::vector<Box> boxes = 
        _opts.box_fname ? load_boxes_from_file(_opts.box_fname) : 
        std::vector<Box>({ {300, 400, 700, 700} });

    if (boxes.empty()) {
        fprintf(stderr, "WARNING: no boxes from %s\n", _opts.box_fname);
        return 1;
    }

    // 扣图
    FrameCrop cropper;
    rc = cropper.open(frame->width, frame->height, (AVPixelFormat)frame->format,
            boxes, _opts.target_width, _opts.target_height);

    // 压缩为文件存储
    std::vector<VideoEnc *> encoders;
    for (int i = 0; i < boxes.size(); i++) {
        char fname[256];
        snprintf(fname, sizeof(fname), "crop-%d_%d.mp4", boxes[i].x1, boxes[i].y1);
        auto enc = new VideoEnc;
        enc->open(fname, _opts.target_width, _opts.target_height);
        encoders.push_back(enc);
    }

    while (stamp <= _opts.from + _opts.duration) {
        if (cropper.put(frame) >= 0) {
            std::vector<AVFrame *> cropped_frames = cropper.get();
            for (int j = 0; j < cropped_frames.size(); j++) {
                if (cropped_frames[j]) {
                    AVFrame *cf = cropped_frames[j];
                    encoders[j]->put_frame(stamp, cf);
                    av_frame_unref(cf);
                }
            }
        }

        av_frame_unref(frame);

        // 下一帧 ..
        rc = input.get_frame(&stamp, &frame);
        if (rc == 0) {
            fprintf(stdout, "DEBUG: EOF, done!!\n");
            break;
        }
        else if (rc < 0) {
            fprintf(stderr, "ERR: rc=%d\n", rc);
            break;
        }
    }

    for (auto enc: encoders) {
        enc->close();
        delete enc;
    }
    cropper.close();
    input.close();
    return 0;
}
