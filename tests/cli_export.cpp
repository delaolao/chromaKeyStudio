// 无界面导出工具: 用来在写 GUI 之前验证整条管线。
//
// 用法: ckcli <输入视频> <输出文件> [webm|prores|qt] [similarity] [blend]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/media.h"

static ck::OutFormat parseFormat(const std::string& s) {
    if (s == "prores") return ck::OutFormat::ProRes4444;
    if (s == "qt") return ck::OutFormat::QuickTimeRGBA;
    if (s == "png" || s == "pngmov") return ck::OutFormat::MovPng;
    return ck::OutFormat::WebM;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: %s <input> <output> [webm|prores|qt] [sim] [blend]\n",
                    argv[0]);
        return 2;
    }
    ck::initFFmpeg();

    ck::Reader reader;
    std::string err;
    if (!reader.open(argv[1], &err)) {
        std::printf("打开失败: %s\n", err.c_str());
        return 1;
    }
    const auto& info = reader.info();
    std::printf("输入 %s  %dx%d  %.2ffps  %.2fs  %s/%s  %s\n", info.path.c_str(),
                info.width, info.height, info.fps, info.duration,
                info.codec.c_str(), info.pixFmt.c_str(),
                info.hasAudio ? "有音轨" : "无音轨");

    ck::ExportOptions opt;
    opt.outPath = argv[2];
    opt.format = argc > 3 ? parseFormat(argv[3]) : ck::OutFormat::WebM;
    ck::Suggestion sug = ck::suggestKey(nullptr, 0, 0, 0);
    (void)sug;
    opt.key.keyR = 0;
    opt.key.keyG = 177;
    opt.key.keyB = 64;
    opt.key.similarity = argc > 4 ? static_cast<float>(std::atof(argv[4])) : 0.30f;
    opt.key.blend = argc > 5 ? static_cast<float>(std::atof(argv[5])) : 0.15f;
    opt.key.despill = true;
    opt.key.despillMix = 0.85f;
    opt.key.spillMatte = true;
    opt.key.neutralize = true;

    auto result = ck::exportVideo(
        reader, opt,
        [](int done, int total) {
            std::printf("\r  %d/%d 帧", done, total);
            std::fflush(stdout);
        },
        nullptr);
    std::printf("\n");
    if (!result.ok) {
        std::printf("导出失败: %s\n", result.error.c_str());
        return 1;
    }
    std::printf("导出完成 %d 帧 %.2fs  %s  %s  音频: %s\n", result.frames,
                result.seconds, result.videoCodec.c_str(), result.pixFmt.c_str(),
                result.audioInfo.c_str());

    auto check = ck::checkAlpha(opt.outPath, opt.format);
    std::printf("门禁: %s  透明占比 %.1f%%\n", check.message.c_str(),
                check.transparentRatio * 100.0);
    return check.ok ? 0 : 1;
}
