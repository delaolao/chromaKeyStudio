#include "media.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace ck {
namespace {

std::string avErr(int code) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buf, sizeof(buf));
    return buf;
}

int gcdInt(int a, int b) {
    while (b) { int t = a % b; a = b; b = t; }
    return a;
}

// 从编码器支持的像素格式里挑一个我们想要的
AVPixelFormat pickPixFmt(const AVCodec* codec,
                         std::initializer_list<AVPixelFormat> prefs) {
    if (!codec->pix_fmts) return prefs.size() ? *prefs.begin() : AV_PIX_FMT_NONE;
    for (auto want : prefs) {
        for (const AVPixelFormat* p = codec->pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
            if (*p == want) return want;
        }
    }
    return codec->pix_fmts[0];
}

}  // namespace

const char* formatSuffix(OutFormat f) {
    switch (f) {
        case OutFormat::WebM: return ".webm";
        case OutFormat::ProRes4444: return ".mov";
        case OutFormat::QuickTimeRGBA: return ".mov";
        case OutFormat::MovPng: return ".mov";
    }
    return ".mov";
}

const char* formatLabel(OutFormat f) {
    switch (f) {
        case OutFormat::WebM: return "WebM (VP9 + Alpha)";
        case OutFormat::ProRes4444: return "ProRes 4444 (MOV + Alpha)";
        case OutFormat::QuickTimeRGBA: return "MOV + QuickTime RGBA";
        case OutFormat::MovPng: return "MOV + PNG (RGBA)";
    }
    return "";
}

void initFFmpeg() { av_log_set_level(AV_LOG_ERROR); }

void MediaInfo::fit(int maxW, int maxH, int* outW, int* outH) const {
    const double r = std::min({static_cast<double>(maxW) / width,
                               static_cast<double>(maxH) / height, 1.0});
    int w = std::max(2, static_cast<int>(width * r) / 2 * 2);
    int h = std::max(2, static_cast<int>(height * r) / 2 * 2);
    *outW = w;
    *outH = h;
}

// ---------------------------------------------------------------- Reader

Reader::~Reader() { close(); }

bool Reader::openDecoder(int index, AVCodecContext** out, std::string* err) {
    if (index < 0) return false;
    AVStream* st = fmt_->streams[index];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) {
        *err = std::string("找不到解码器: ") +
               avcodec_get_name(st->codecpar->codec_id);
        return false;
    }
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    if (!ctx) { *err = "分配解码器失败"; return false; }
    if (avcodec_parameters_to_context(ctx, st->codecpar) < 0) {
        avcodec_free_context(&ctx);
        *err = "复制解码参数失败";
        return false;
    }
    ctx->pkt_timebase = st->time_base;
    if (avcodec_open2(ctx, dec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        *err = "打开解码器失败";
        return false;
    }
    *out = ctx;
    return true;
}

bool Reader::open(const std::string& path, std::string* err) {
    close();
    int ret = avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr);
    if (ret < 0) { *err = "打不开文件: " + avErr(ret); return false; }
    ret = avformat_find_stream_info(fmt_, nullptr);
    if (ret < 0) { *err = "读取流信息失败: " + avErr(ret); fmt_ = nullptr; return false; }

    videoIndex_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audioIndex_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (videoIndex_ < 0) {
        *err = "文件里没有视频流";
        close();
        return false;
    }
    if (!openDecoder(videoIndex_, &videoDec_, err)) { close(); return false; }
    if (audioIndex_ >= 0) {
        std::string e;
        if (openDecoder(audioIndex_, &audioDec_, &e)) {
            info_.hasAudio = true;
            info_.audioChannels = audioDec_->ch_layout.nb_channels;
            info_.audioRate = audioDec_->sample_rate;
            info_.audioCodec = avcodec_get_name(audioDec_->codec_id);
            info_.audioBitrate = static_cast<int>(fmt_->streams[audioIndex_]->codecpar->bit_rate);
        }
    }

    AVStream* vs = fmt_->streams[videoIndex_];
    info_.path = path;
    info_.width = videoDec_->width;
    info_.height = videoDec_->height;
    info_.codec = avcodec_get_name(videoDec_->codec_id);
    const char* pn = av_get_pix_fmt_name(videoDec_->pix_fmt);
    info_.pixFmt = pn ? pn : "";
    AVRational fr = vs->avg_frame_rate.num ? vs->avg_frame_rate : vs->r_frame_rate;
    info_.fps = fr.num > 0 && fr.den > 0 ? av_q2d(fr) : 25.0;
    if (info_.fps <= 0.0 || info_.fps > 240.0) info_.fps = 25.0;
    info_.duration = fmt_->duration > 0 ? fmt_->duration / static_cast<double>(AV_TIME_BASE)
                                        : (vs->duration > 0 ? vs->duration * av_q2d(vs->time_base) : 0.0);

    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (!frame_ || !packet_) { *err = "分配帧失败"; close(); return false; }
    return true;
}

void Reader::close() {
    if (sws_) { sws_freeContext(sws_); sws_ = nullptr; }
    if (frame_) av_frame_free(&frame_);
    if (packet_) av_packet_free(&packet_);
    if (videoDec_) avcodec_free_context(&videoDec_);
    if (audioDec_) avcodec_free_context(&audioDec_);
    if (fmt_) avformat_close_input(&fmt_);
    swsW_ = swsH_ = swsDstW_ = swsDstH_ = 0;
    swsFmt_ = -1;
    videoIndex_ = audioIndex_ = -1;
    sequential_ = demuxEof_ = decoderFlushed_ = false;
    pendingAudio_.clear();
    info_ = MediaInfo();
}

// 把解出来的 AVFrame 转成 RGB24(缩放)
bool Reader::convertFrame(const AVFrame* src, RgbFrame& out, std::string* err) {
    const int dw = sequential_ ? info_.width : outDstW_;
    const int dh = sequential_ ? info_.height : outDstH_;
    if (dw <= 0 || dh <= 0) { *err = "无效输出尺寸"; return false; }
    if (!sws_ || swsW_ != src->width || swsH_ != src->height ||
        swsFmt_ != src->format || swsDstW_ != dw || swsDstH_ != dh) {
        if (sws_) sws_freeContext(sws_);
        sws_ = sws_getContext(src->width, src->height,
                              static_cast<AVPixelFormat>(src->format), dw, dh,
                              AV_PIX_FMT_RGB24,
                              SWS_BILINEAR | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INT,
                              nullptr, nullptr, nullptr);
        if (!sws_) { *err = "创建缩放上下文失败"; return false; }
        swsW_ = src->width; swsH_ = src->height;
        swsFmt_ = src->format; swsDstW_ = dw; swsDstH_ = dh;
    }
    out.resize(dw, dh);
    uint8_t* dstData[4] = {out.data.data(), nullptr, nullptr, nullptr};
    int dstLines[4] = {out.stride, 0, 0, 0};
    sws_scale(sws_, src->data, src->linesize, 0, src->height, dstData, dstLines);
    return true;
}

bool Reader::frameAt(double t, int outW, int outH, RgbFrame& out, std::string* err) {
    if (!fmt_) { *err = "文件未打开"; return false; }
    sequential_ = false;
    outDstW_ = outW;
    outDstH_ = outH;

    AVStream* vs = fmt_->streams[videoIndex_];
    const int64_t target = static_cast<int64_t>(t / av_q2d(vs->time_base));
    avcodec_flush_buffers(videoDec_);
    if (av_seek_frame(fmt_, videoIndex_, std::max<int64_t>(0, target),
                      AVSEEK_FLAG_BACKWARD) < 0) {
        // 有些容器不支持 seek, 退化成从头解码
        av_seek_frame(fmt_, videoIndex_, 0, AVSEEK_FLAG_BACKWARD);
    }
    avcodec_flush_buffers(videoDec_);

    bool got = false;
    while (!got) {
        int ret = av_read_frame(fmt_, packet_);
        if (ret < 0) break;
        if (packet_->stream_index == videoIndex_) {
            if (avcodec_send_packet(videoDec_, packet_) >= 0) {
                while (true) {
                    int r2 = avcodec_receive_frame(videoDec_, frame_);
                    if (r2 == AVERROR(EAGAIN) || r2 == AVERROR_EOF) break;
                    if (r2 < 0) break;
                    got = true;
                    break;
                }
            }
        }
        av_packet_unref(packet_);
        if (got) break;
    }
    if (!got) {
        // 交给解码器冲刷
        avcodec_send_packet(videoDec_, nullptr);
        if (avcodec_receive_frame(videoDec_, frame_) >= 0) got = true;
    }
    if (!got) { *err = "定位取帧失败"; return false; }
    const bool ok = convertFrame(frame_, out, err);
    av_frame_unref(frame_);
    return ok;
}

bool Reader::startSequential(std::string* err) {
    if (!fmt_) { *err = "文件未打开"; return false; }
    sequential_ = true;
    demuxEof_ = false;
    decoderFlushed_ = false;
    outDstW_ = info_.width;
    outDstH_ = info_.height;
    avcodec_flush_buffers(videoDec_);
    if (av_seek_frame(fmt_, -1, 0, AVSEEK_FLAG_BACKWARD) >= 0) {
        avcodec_flush_buffers(videoDec_);
        if (audioDec_) avcodec_flush_buffers(audioDec_);
    }
    for (auto* p : pendingAudio_) av_packet_free(&p);
    pendingAudio_.clear();
    return true;
}

bool Reader::nextFrame(RgbFrame& out, bool* done, std::string* err) {
    *done = false;
    while (true) {
        int ret = avcodec_receive_frame(videoDec_, frame_);
        if (ret == 0) {
            const bool ok = convertFrame(frame_, out, err);
            av_frame_unref(frame_);
            return ok;
        }
        if (ret == AVERROR_EOF) { *done = true; return true; }
        if (ret != AVERROR(EAGAIN)) { *err = "解码失败: " + avErr(ret); return false; }

        if (demuxEof_) {
            if (!decoderFlushed_) {
                avcodec_send_packet(videoDec_, nullptr);
                decoderFlushed_ = true;
                continue;
            }
            *done = true;
            return true;
        }
        int r = av_read_frame(fmt_, packet_);
        if (r < 0) { demuxEof_ = true; continue; }
        if (packet_->stream_index == videoIndex_) {
            avcodec_send_packet(videoDec_, packet_);
        } else if (packet_->stream_index == audioIndex_) {
            // 音频包留给导出器, 保证音画交织
            AVPacket* copy = av_packet_alloc();
            av_packet_move_ref(copy, packet_);
            pendingAudio_.push_back(copy);
        }
        av_packet_unref(packet_);
    }
}

std::vector<AVPacket*> Reader::takeAudioPackets() {
    std::vector<AVPacket*> out;
    out.swap(pendingAudio_);
    return out;
}

}  // namespace ck
