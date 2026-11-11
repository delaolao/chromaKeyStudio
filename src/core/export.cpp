// 导出管线 + 导出后的 Alpha 门禁。
#include <algorithm>
#include <cmath>
#include <cstring>

#include "media.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
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

const char* muxerName(OutFormat f) {
    return f == OutFormat::WebM ? "webm" : "mov";
}

}  // namespace

ExportResult exportVideo(Reader& reader, const ExportOptions& opt,
                         ProgressFn progress, CancelFn cancel) {
    ExportResult res;
    const MediaInfo& info = reader.info();
    const int W = info.width;
    const int H = info.height;
    const bool wantAudio = opt.includeAudio && info.hasAudio;

    AVFormatContext* ofmt = nullptr;
    AVCodecContext* venc = nullptr;
    AVCodecContext* aenc = nullptr;
    AVStream* vst = nullptr;
    AVStream* ast = nullptr;
    SwsContext* toEnc = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* vframe = nullptr;
    AVFrame* aframe = nullptr;
    AVPacket* pkt = nullptr;
    std::vector<uint8_t> rgbaBuf;
    bool headerWritten = false;
    int64_t audioPts = 0;

    auto fail = [&](const std::string& msg) {
        res.ok = false;
        res.error = msg;
        if (swr) swr_free(&swr);
        if (toEnc) sws_freeContext(toEnc);
        if (vframe) av_frame_free(&vframe);
        if (aframe) av_frame_free(&aframe);
        if (pkt) av_packet_free(&pkt);
        if (venc) avcodec_free_context(&venc);
        if (aenc) avcodec_free_context(&aenc);
        if (ofmt) {
            if (headerWritten) av_write_trailer(ofmt);
            if (!(ofmt->oformat->flags & AVFMT_NOFILE) && ofmt->pb) avio_closep(&ofmt->pb);
            avformat_free_context(ofmt);
        }
        return res;
    };

    int ret = avformat_alloc_output_context2(&ofmt, nullptr, muxerName(opt.format),
                                             opt.outPath.c_str());
    if (ret < 0 || !ofmt) return fail("无法创建输出容器: " + avErr(ret));

    // ---------------- 视频编码器
    const AVCodec* vcodec = nullptr;
    AVPixelFormat vpix = AV_PIX_FMT_NONE;
    switch (opt.format) {
        case OutFormat::WebM:
            vcodec = avcodec_find_encoder_by_name("libvpx-vp9");
            vpix = AV_PIX_FMT_YUVA420P;
            break;
        case OutFormat::ProRes4444:
            vcodec = avcodec_find_encoder_by_name("prores_ks");
            vpix = AV_PIX_FMT_YUVA444P10LE;
            break;
        case OutFormat::QuickTimeRGBA:
            vcodec = avcodec_find_encoder(AV_CODEC_ID_QTRLE);
            vpix = AV_PIX_FMT_NONE;  // 下面从支持列表里挑
            break;
        case OutFormat::MovPng:
            vcodec = avcodec_find_encoder(AV_CODEC_ID_PNG);
            vpix = AV_PIX_FMT_RGBA;
            break;
    }
    if (!vcodec) return fail("找不到视频编码器(该构建可能缺对应模块)");
    if (opt.format == OutFormat::QuickTimeRGBA) {
        static const AVPixelFormat prefs[] = {AV_PIX_FMT_RGBA, AV_PIX_FMT_ARGB,
                                              AV_PIX_FMT_RGB32, AV_PIX_FMT_RGB24};
        for (auto want : prefs) {
            for (const AVPixelFormat* p = vcodec->pix_fmts;
                 p && *p != AV_PIX_FMT_NONE; ++p) {
                if (*p == want) { vpix = want; break; }
            }
            if (vpix != AV_PIX_FMT_NONE) break;
        }
        if (vpix == AV_PIX_FMT_NONE && vcodec->pix_fmts) vpix = vcodec->pix_fmts[0];
    }

    venc = avcodec_alloc_context3(vcodec);
    if (!venc) return fail("分配视频编码器失败");
    venc->width = W;
    venc->height = H;
    venc->pix_fmt = vpix;
    venc->time_base = AVRational{1, static_cast<int>(std::lround(info.fps))};
    venc->framerate = AVRational{static_cast<int>(std::lround(info.fps)), 1};
    venc->gop_size = static_cast<int>(std::lround(info.fps)) * 2;
    venc->max_b_frames = 0;

    if (opt.format == OutFormat::WebM) {
        venc->bit_rate = 0;
        av_opt_set(venc->priv_data, "crf", std::to_string(opt.crf).c_str(), 0);
        av_opt_set(venc->priv_data, "cpu-used", std::to_string(opt.speed).c_str(), 0);
        av_opt_set(venc->priv_data, "auto-alt-ref", "0", 0);
        av_opt_set(venc->priv_data, "row-mt", "1", 0);
        av_opt_set(venc->priv_data, "deadline", "good", 0);
    } else if (opt.format == OutFormat::ProRes4444) {
        av_opt_set(venc->priv_data, "profile", "4", 0);   // 4 = 4444
        av_opt_set(venc->priv_data, "vendor", "apl0", 0);
    }

    if (avcodec_open2(venc, vcodec, nullptr) < 0)
        return fail("打开视频编码器失败");

    vst = avformat_new_stream(ofmt, nullptr);
    if (!vst) return fail("创建视频流失败");
    vst->codecpar->codec_tag = 0;
    avcodec_parameters_from_context(vst->codecpar, venc);
    vst->time_base = venc->time_base;

    // ---------------- 音频编码器
    if (wantAudio) {
        const AVCodec* acodec = nullptr;
        if (opt.format == OutFormat::WebM) {
            acodec = avcodec_find_encoder_by_name("libopus");
        } else {
            acodec = avcodec_find_encoder(AV_CODEC_ID_PCM_S24LE);
        }
        if (acodec) {
            aenc = avcodec_alloc_context3(acodec);
            aenc->sample_rate = opt.format == OutFormat::WebM ? 48000 : info.audioRate;
            aenc->sample_fmt = aenc->codec->sample_fmts ? aenc->codec->sample_fmts[0]
                                                       : AV_SAMPLE_FMT_FLT;
            const int chans = std::min(2, std::max(1, info.audioChannels));
            av_channel_layout_default(&aenc->ch_layout, chans);
            aenc->time_base = AVRational{1, aenc->sample_rate};
            if (opt.format == OutFormat::WebM) {
                const int srcKbps = info.audioBitrate / 1000;
                int kbps = std::max(aenc->ch_layout.nb_channels == 1 ? 96 : 160, srcKbps);
                kbps = std::min(kbps, 448);
                aenc->bit_rate = kbps * 1000;
                av_opt_set(aenc->priv_data, "application", "audio", 0);
            } else {
                aenc->bit_rate = 0;
            }
            if (avcodec_open2(aenc, acodec, nullptr) < 0) {
                avcodec_free_context(&aenc);
            } else {
                ast = avformat_new_stream(ofmt, nullptr);
                ast->codecpar->codec_tag = 0;
                avcodec_parameters_from_context(ast->codecpar, aenc);
                ast->time_base = aenc->time_base;

                AVCodecContext* adec = reader.audioDecContext();
                swr = nullptr;
                AVChannelLayout inLayout;
                av_channel_layout_copy(&inLayout, &adec->ch_layout);
                int r = swr_alloc_set_opts2(&swr, &aenc->ch_layout, aenc->sample_fmt,
                                            aenc->sample_rate, &inLayout,
                                            adec->sample_fmt, adec->sample_rate, 0,
                                            nullptr);
                av_channel_layout_uninit(&inLayout);
                if (r < 0 || !swr || swr_init(swr) < 0) {
                    swr_free(&swr);
                    ast = nullptr;
                    avcodec_free_context(&aenc);
                }
            }
        }
    }

    if (avio_open(&ofmt->pb, opt.outPath.c_str(), AVIO_FLAG_WRITE) < 0)
        return fail("无法写入输出文件");
    if (avformat_write_header(ofmt, nullptr) < 0)
        return fail("写入文件头失败(格式参数不匹配?)");
    headerWritten = true;

    pkt = av_packet_alloc();
    vframe = av_frame_alloc();

    // RGBA -> 编码器像素格式
    toEnc = sws_getContext(W, H, AV_PIX_FMT_RGBA, W, H, vpix, SWS_BILINEAR, nullptr,
                           nullptr, nullptr);
    if (!toEnc) return fail("创建颜色转换上下文失败");
    rgbaBuf.resize(static_cast<size_t>(W) * H * 4);

    // 音频重采样缓冲
    std::vector<uint8_t> audioBuf;
    const int maxOutSamples =
        swr && aenc ? static_cast<int>(av_rescale_rnd(
                          swr_get_delay(swr, aenc->sample_rate) + 4096,
                          aenc->sample_rate, aenc->sample_rate, AV_ROUND_UP))
                    : 0;
    // 音频帧一律按固定容量分配, 每次写入的量都夹在这个容量内 ——
    // 之前按 frame_size(960) 分配、冲刷时却写 4096, 会直接写坏堆
    constexpr int kAudioCap = 8192;
    if (aenc) {
        aframe = av_frame_alloc();
        aframe->format = aenc->sample_fmt;
        av_channel_layout_copy(&aframe->ch_layout, &aenc->ch_layout);
        aframe->sample_rate = aenc->sample_rate;
        aframe->nb_samples = kAudioCap;
        if (av_frame_get_buffer(aframe, 0) < 0) return fail("分配音频帧失败");
    }

    auto writeVideoPacket = [&](AVPacket* p) {
        av_packet_rescale_ts(p, venc->time_base, vst->time_base);
        p->stream_index = vst->index;
        av_interleaved_write_frame(ofmt, p);
        av_packet_unref(p);
    };

    auto encodeAudioFrame = [&](AVFrame* f) {
        if (avcodec_send_frame(aenc, f) < 0) return;
        AVPacket* ap = av_packet_alloc();
        while (avcodec_receive_packet(aenc, ap) == 0) {
            av_packet_rescale_ts(ap, aenc->time_base, ast->time_base);
            ap->stream_index = ast->index;
            av_interleaved_write_frame(ofmt, ap);
            av_packet_unref(ap);
        }
        av_packet_free(&ap);
    };

    // 音频包 -> 解码 -> 重采样 -> 编码
    auto handleAudioPackets = [&](std::vector<AVPacket*>& packets) {
        if (!aenc || !swr) {
            for (auto* p : packets) av_packet_free(&p);
            packets.clear();
            return;
        }
        AVCodecContext* adec = reader.audioDecContext();
        AVFrame* dframe = av_frame_alloc();
        for (auto* p : packets) {
            if (avcodec_send_packet(adec, p) >= 0) {
                while (avcodec_receive_frame(adec, dframe) == 0) {
                    const int outSamples = static_cast<int>(av_rescale_rnd(
                        swr_get_delay(swr, adec->sample_rate) + dframe->nb_samples,
                        aenc->sample_rate, adec->sample_rate, AV_ROUND_UP));
                    if (outSamples > 0) {
                        int cap = kAudioCap;
                        if (aenc->frame_size > 0) cap = std::min(cap, aenc->frame_size);
                        const int want = std::min(outSamples, cap);
                        av_frame_make_writable(aframe);
                        uint8_t* out8[8] = {nullptr};
                        const int got = swr_convert(
                            swr, aframe->data, want,
                            const_cast<const uint8_t**>(dframe->extended_data),
                            dframe->nb_samples);
                        if (got > 0) {
                            aframe->nb_samples = got;
                            aframe->pts = audioPts;
                            audioPts += got;
                            encodeAudioFrame(aframe);
                        }
                    }
                    av_frame_unref(dframe);
                }
            }
            av_packet_free(&p);
        }
        av_frame_free(&dframe);
        packets.clear();
    };

    const int total = std::max(1, static_cast<int>(info.duration * info.fps + 0.5));
    Image keyed;

    // 多边形遮罩是静态的, 按导出分辨率预计算一次, 每帧只做一次查表相乘
    MaskLayers maskLayers;
    if (!opt.masks.empty())
        maskLayers = rasterizeMask(opt.masks, W, H, opt.maskSettings);
    int frames = 0;
    const bool seqOk = reader.startSequential(&res.error);
    if (!seqOk) return fail(res.error.empty() ? "无法开始解码" : res.error);

    while (true) {
        if (cancel && cancel()) return fail("已取消");
        RgbFrame rgb;
        bool done = false;
        std::string derr;
        if (!reader.nextFrame(rgb, &done, &derr)) return fail(derr);
        if (done) break;

        keyImage(rgb.data.data(), rgb.width, rgb.height, rgb.stride, opt.key, keyed);
        if (!maskLayers.empty())
            applyMask(keyed, maskLayers, opt.key.keyR, opt.key.keyG, opt.key.keyB);

        // RGBA -> 编码器格式
        av_frame_unref(vframe);
        vframe->format = vpix;
        vframe->width = W;
        vframe->height = H;
        if (av_frame_get_buffer(vframe, 0) < 0) return fail("分配视频帧失败");
        const uint8_t* srcData[4] = {keyed.data.data(), nullptr, nullptr, nullptr};
        int srcLines[4] = {keyed.stride, 0, 0, 0};
        sws_scale(toEnc, srcData, srcLines, 0, H, vframe->data, vframe->linesize);
        vframe->pts = frames;

        if (avcodec_send_frame(venc, vframe) < 0) return fail("编码视频帧失败");
        while (avcodec_receive_packet(venc, pkt) == 0) writeVideoPacket(pkt);

        auto audio = reader.takeAudioPackets();
        if (!audio.empty()) handleAudioPackets(audio);

        ++frames;
        if (progress && (frames % 3 == 0 || frames == total)) progress(frames, total);
    }
    // 冲刷视频
    avcodec_send_frame(venc, nullptr);
    while (avcodec_receive_packet(venc, pkt) == 0) writeVideoPacket(pkt);

    // 冲刷音频解码/编码
    if (aenc && swr) {
        AVCodecContext* adec = reader.audioDecContext();
        avcodec_send_packet(adec, nullptr);
        AVFrame* dframe = av_frame_alloc();
        while (avcodec_receive_frame(adec, dframe) == 0) {
            uint8_t* outBuf = nullptr;
            const int outSamples = static_cast<int>(av_rescale_rnd(
                swr_get_delay(swr, adec->sample_rate) + dframe->nb_samples,
                aenc->sample_rate, adec->sample_rate, AV_ROUND_UP));
            if (outSamples > 0) {
                int cap = kAudioCap;
                if (aenc->frame_size > 0) cap = std::min(cap, aenc->frame_size);
                av_frame_make_writable(aframe);
                const int got = swr_convert(swr, aframe->data, std::min(outSamples, cap),
                                            const_cast<const uint8_t**>(dframe->extended_data),
                                            dframe->nb_samples);
                if (got > 0) {
                    aframe->nb_samples = got;
                    aframe->pts = audioPts;
                    audioPts += got;
                    encodeAudioFrame(aframe);
                }
            }
            av_frame_unref(dframe);
        }
        av_frame_free(&dframe);
        if (swr) {
            const int outSamples = static_cast<int>(av_rescale_rnd(
                swr_get_delay(swr, aenc->sample_rate), aenc->sample_rate,
                aenc->sample_rate, AV_ROUND_UP));
            if (outSamples > 0) {
                int cap = kAudioCap;
                if (aenc->frame_size > 0) cap = std::min(cap, aenc->frame_size);
                av_frame_make_writable(aframe);
                const int got = swr_convert(swr, aframe->data, std::min(outSamples, cap),
                                            nullptr, 0);
                if (got > 0) {
                    aframe->nb_samples = got;
                    aframe->pts = audioPts;
                    audioPts += got;
                    encodeAudioFrame(aframe);
                }
            }
        }
        encodeAudioFrame(nullptr);
    }
    av_write_trailer(ofmt);

    res.ok = true;
    res.frames = frames;
    res.seconds = frames / std::max(info.fps, 1e-6);
    res.videoCodec = vcodec->name;
    res.pixFmt = av_get_pix_fmt_name(vpix) ? av_get_pix_fmt_name(vpix) : "";
    if (ast) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s %dHz %dch", aenc->codec->name,
                      aenc->sample_rate, aenc->ch_layout.nb_channels);
        res.audioInfo = buf;
    } else {
        res.audioInfo = info.hasAudio ? "未写入" : "源文件没有音轨";
    }

    if (swr) swr_free(&swr);
    if (toEnc) sws_freeContext(toEnc);
    if (vframe) av_frame_free(&vframe);
    if (aframe) av_frame_free(&aframe);
    if (pkt) av_packet_free(&pkt);
    if (venc) avcodec_free_context(&venc);
    if (aenc) avcodec_free_context(&aenc);
    if (!(ofmt->oformat->flags & AVFMT_NOFILE) && ofmt->pb) avio_closep(&ofmt->pb);
    avformat_free_context(ofmt);
    (void)maxOutSamples;
    (void)audioBuf;
    return res;
}

// ---------------------------------------------------------------- 门禁

AlphaCheck checkAlpha(const std::string& path, OutFormat fmt) {
    AlphaCheck out;
    AVFormatContext* fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, path.c_str(), nullptr, nullptr) < 0) {
        out.message = "打不开输出文件";
        return out;
    }
    avformat_find_stream_info(fmtCtx, nullptr);
    const int idx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (idx < 0) {
        out.message = "输出文件里没有视频流";
        avformat_close_input(&fmtCtx);
        return out;
    }
    AVStream* st = fmtCtx->streams[idx];
    const char* pixName =
        av_get_pix_fmt_name(static_cast<AVPixelFormat>(st->codecpar->format));
    const std::string pix = pixName ? pixName : "";

    // 容器层检查
    bool containerOk = true;
    if (fmt == OutFormat::WebM) {
        AVDictionaryEntry* e = av_dict_get(st->metadata, "alpha_mode", nullptr, 0);
        containerOk = e != nullptr;
        out.message = containerOk ? "容器标记 alpha_mode=1" :
                                    "容器里没有 alpha_mode 标记";
    } else {
        containerOk = pix.find("yuva") != std::string::npos ||
                      pix == "rgba" || pix == "argb" || pix == "bgra";
        out.message = "pix_fmt=" + pix;
    }

    // 数据层: 真解一帧看 alpha
    const AVCodec* dec = nullptr;
    if (fmt == OutFormat::WebM) {
        dec = avcodec_find_decoder_by_name("libvpx-vp9");
        if (!dec) dec = avcodec_find_decoder(AV_CODEC_ID_VP9);
    } else {
        dec = avcodec_find_decoder(st->codecpar->codec_id);
    }
    if (!dec) {
        out.message += "; 找不到解码器";
        avformat_close_input(&fmtCtx);
        return out;
    }
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(ctx, st->codecpar);
    if (avcodec_open2(ctx, dec, nullptr) < 0) {
        out.message += "; 打开解码器失败";
        avcodec_free_context(&ctx);
        avformat_close_input(&fmtCtx);
        return out;
    }

    // 跳到中间取一帧
    const int64_t dur = fmtCtx->duration > 0 ? fmtCtx->duration : 0;
    if (dur > 0) av_seek_frame(fmtCtx, -1, dur / 2, AVSEEK_FLAG_BACKWARD);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    SwsContext* sws = nullptr;
    std::vector<uint8_t> rgba;
    bool decoded = false;
    int64_t transparent = 0, pixels = 0;

    while (!decoded) {
        int r = av_read_frame(fmtCtx, pkt);
        if (r < 0) break;
        if (pkt->stream_index == idx && avcodec_send_packet(ctx, pkt) >= 0) {
            if (avcodec_receive_frame(ctx, frame) == 0) {
                if (!sws) {
                    sws = sws_getContext(frame->width, frame->height,
                                         static_cast<AVPixelFormat>(frame->format),
                                         frame->width, frame->height, AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
                    rgba.resize(static_cast<size_t>(frame->width) * frame->height * 4);
                }
                if (sws) {
                    uint8_t* dst[4] = {rgba.data(), nullptr, nullptr, nullptr};
                    int lines[4] = {frame->width * 4, 0, 0, 0};
                    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst,
                              lines);
                    for (int y = 0; y < frame->height; ++y) {
                        const uint8_t* row = rgba.data() + static_cast<size_t>(y) *
                                             frame->width * 4;
                        for (int x = 0; x < frame->width; ++x) {
                            if (row[x * 4 + 3] < 250) ++transparent;
                            ++pixels;
                        }
                    }
                    decoded = true;
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

    if (sws) sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmtCtx);

    if (!decoded) {
        out.message += "; 解码不出帧";
        return out;
    }
    out.transparentRatio = pixels ? static_cast<double>(transparent) / pixels : 0.0;
    out.ok = containerOk && out.transparentRatio > 0.001;
    if (out.transparentRatio <= 0.001) out.message += "; 解码后 alpha 全不透明";
    return out;
}

}  // namespace ck
