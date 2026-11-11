// 多边形遮罩(垃圾遮罩 / garbage matte)。
//
// 用途: 绿幕上的瑕疵(水印、夹具、接缝)不在四边, 用任意多边形圈掉。
// 三种模式:
//   Transparent —— 区内 alpha 强制 0(去掉水印、穿帮物)
//   HoldOut     —— 区内 alpha 强制 255(抠像把主体吃掉时救回来)
//   FillKey     —— 区内先填成键色再参与抠像(半透明水印的白边用这个更干净)
//
// 这一层不依赖 Qt: ckcli 也能测, 避免把 UI 依赖带进核心。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "keying.h"

namespace ck {

struct MaskPoint {
    float x = 0.0f;  // 归一化 0..1
    float y = 0.0f;
};

enum class MaskMode { Transparent = 0, HoldOut = 1, FillKey = 2 };

const char* maskModeName(MaskMode m);      // 内部名(JSON)
const char* maskModeLabel(MaskMode m);     // 界面名
bool parseMaskMode(const std::string& s, MaskMode* out);

struct MaskPolygon {
    std::vector<MaskPoint> points;
    MaskMode mode = MaskMode::Transparent;
    bool enabled = true;
    bool closed = true;
};

struct MaskSettings {
    float featherPx = 1.5f;  // 羽化半径(以目标分辨率像素计)
    float expandPx = 0.0f;   // 外扩(正)/内缩(负)
};

// 一张掩码: 每种模式一张覆盖图(0..255)
struct MaskLayers {
    std::vector<uint8_t> transparent;
    std::vector<uint8_t> holdOut;
    std::vector<uint8_t> fillKey;
    int width = 0;
    int height = 0;
    bool empty() const { return transparent.empty() && holdOut.empty() && fillKey.empty(); }
};

// 把多边形栅格化成覆盖图(按目标分辨率)。没有对应模式的多边形就留空。
MaskLayers rasterizeMask(const std::vector<MaskPolygon>& polys, int width, int height,
                         const MaskSettings& settings);

// 把掩码应用到 RGBA 图上(alpha 与颜色)
void applyMask(Image& rgba, const MaskLayers& layers, uint8_t keyR, uint8_t keyG,
               uint8_t keyB);

// ---- 存取: 与视频同目录的 .ckmask.json
std::string maskToJson(const std::vector<MaskPolygon>& polys, const MaskSettings& s);
bool maskFromJson(const std::string& text, std::vector<MaskPolygon>* polys,
                  MaskSettings* settings);
std::string defaultMaskPath(const std::string& videoPath);

}  // namespace ck
