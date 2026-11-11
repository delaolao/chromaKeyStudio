// FFmpeg 进程内封装: 解封装/解码/编码/封装。
//
// 与 Python 版最大的结构差异: 那边是 "ffmpeg.exe 子进程 + 裸流管道",
// 这边直接把 libav* 链进 exe。好处是没有进程启动开销、能精确控制
// 每一帧、并且便携包不需要额外带 200MB 的 ffmpeg.exe。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "keying.h"
#include "mask.h"

struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;

namespace ck {

enum class OutFormat { WebM, ProRes4444, QuickTimeRGBA, MovPng };

const char* formatSuffix(OutFormat f);
const char* formatLabel(OutFormat f);

// 解码出来的 RGB 帧(紧密排列, stride = width*3)
struct RgbFrame {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int stride = 0;
    void resize(int w, int h) {
        width = w;
        height = h;
        stride = w * 3;
        data.assign(static_cast<size_t>(stride) * h, 0);
    }
};

struct MediaInfo {
    std::string path;
    int width = 0;
    int height = 0;
    double fps = 25.0;
    double duration = 0.0;
    std::string codec;
    std::string pixFmt;
    bool hasAudio = false;
    std::string audioCodec;
    int audioChannels = 0;
    int audioRate = 0;
    int audioBitrate = 0;

    int frameCount() const { return static_cast<int>(duration * fps + 0.5); }
    void fit(int maxW, int maxH, int* outW, int* outH) const;
};

// 读一帧/顺序读帧
class Reader {
public:
    Reader() = default;
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;

    bool open(const std::string& path, std::string* err);
    void close();
    const MediaInfo& info() const { return info_; }
    bool isOpen() const { return fmt_ != nullptr; }

    // 定位到 t 秒并解出第一帧, 转成指定尺寸的 RGBA
    bool frameAt(double t, int outW, int outH, RgbFrame& out, std::string* err);

    // 导出用: 从头顺序解码, 每帧转成原始尺寸 RGBA
    bool startSequential(std::string* err);
    // 返回 false 表示没有更多帧; err 非空表示出错
    bool nextFrame(RgbFrame& out, bool* done, std::string* err);

    // 音频: 供导出器使用
    AVFormatContext* formatContext() const { return fmt_; }
    int videoStreamIndex() const { return videoIndex_; }
    int audioStreamIndex() const { return audioIndex_; }
    AVCodecContext* audioDecContext() const { return audioDec_; }

    // 顺序解码时攒下来的音频包, 交给导出器去编码
    std::vector<AVPacket*> takeAudioPackets();

private:
    bool openDecoder(int streamIndex, AVCodecContext** ctx, std::string* err);
    bool convertFrame(const AVFrame* src, RgbFrame& out, std::string* err);

    std::vector<AVPacket*> pendingAudio_;
    int outDstW_ = 0;
    int outDstH_ = 0;

    AVFormatContext* fmt_ = nullptr;
    AVCodecContext* videoDec_ = nullptr;
    AVCodecContext* audioDec_ = nullptr;
    int videoIndex_ = -1;
    int audioIndex_ = -1;
    MediaInfo info_;

    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ = nullptr;
    int swsW_ = 0, swsH_ = 0, swsFmt_ = -1, swsDstW_ = 0, swsDstH_ = 0;

    bool sequential_ = false;
    bool demuxEof_ = false;
    bool decoderFlushed_ = false;
    int64_t nextPts_ = 0;
};

struct ExportOptions {
    std::string outPath;
    OutFormat format = OutFormat::WebM;
    KeyParams key;
    bool includeAudio = true;
    int crf = 30;        // 仅 WebM
    int speed = 3;       // libvpx -cpu-used
    bool faststart = true;

    // 多边形遮罩(垃圾遮罩): 空表示不用
    std::vector<MaskPolygon> masks;
    MaskSettings maskSettings;
};

struct ExportResult {
    bool ok = false;
    std::string error;
    int frames = 0;
    double seconds = 0.0;
    double sizeMb = 0.0;
    std::string videoCodec;
    std::string pixFmt;
    std::string audioInfo;
};

using ProgressFn = std::function<void(int done, int total)>;
using CancelFn = std::function<bool()>;

ExportResult exportVideo(Reader& reader, const ExportOptions& opt,
                         ProgressFn progress, CancelFn cancel);

// 导出后校验: 容器里有 alpha 吗? 真的能解出透明像素吗?
struct AlphaCheck {
    bool ok = false;
    std::string message;
    double transparentRatio = 0.0;
};
AlphaCheck checkAlpha(const std::string& path, OutFormat fmt);

// 全局初始化/清理(libav 的注册)
void initFFmpeg();
std::string lastAvError(int code);

}  // namespace ck
