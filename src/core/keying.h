// 键控核心 —— 与 Python 版 (app/core/keying.py) 同一套数学。
//
// 为什么保持一致: Python 版已经用真实素材和真机验证过(包括 pet_player 的透明性),
// C++ 重写的价值在于体积和速度, 不能顺手把画质改坏。所以公式照抄, 连
// 亮度地板(64)、透明区填黑、溢色覆盖率取 min 这些细节都不动。
//
// 数学回顾:
//   1. 每个通道除以 max(亮度, 64) 做亮度归一化, 再乘 64 回到 uint8 —— 绿幕
//      打光亮暗不匀时 RGB 比例不变, 于是暗角也能一次抠干净。
//   2. d = |ΔR| + |ΔG| + |ΔB| (归一化空间里的曼哈顿距离, 饱和到 255)
//   3. alpha = clamp((d/scale - similarity) / blend, 0, 1)
//   4. excess = main - max(other1, other2): 背景渗进前景的多少。
//      用它反推覆盖率取 min, 修掉细发丝的"绿刺"; 同时它就是 despill 要削的值。
//   5. 完全透明的像素填黑: 4:2:0 下采样时不会把彩色渗进边缘。
#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace ck {

struct KeyParams {
    uint8_t keyR = 0, keyG = 255, keyB = 0;
    float similarity = 0.28f;
    float blend = 0.22f;
    float featherSigma = 0.0f;   // 像素
    bool despill = false;
    float despillMix = 0.6f;
    bool spillMatte = true;
    bool neutralize = true;      // 透明区填黑(导出必须开)
};

// 一张 8 位 RGBA 图(行间可能有 padding)
struct Image {
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int stride = 0;  // 字节

    Image() = default;
    Image(int w, int h) { resize(w, h); }
    void resize(int w, int h);
    uint8_t* row(int y) { return data.data() + static_cast<size_t>(y) * stride; }
    const uint8_t* row(int y) const {
        return data.data() + static_cast<size_t>(y) * stride;
    }
};

// 键控一张 RGB 图, 输出 RGBA。rgb 必须紧密排列(RGBRGB...)。
void keyImage(const uint8_t* rgb, int width, int height, int rgbStride,
              const KeyParams& p, Image& out);

// 从画面四边估计键色和容差(对应 Python 的 suggest_key_and_similarity)
struct Suggestion {
    uint8_t r = 0, g = 255, b = 0;
    float similarity = 0.35f;
};
Suggestion suggestKey(const uint8_t* rgb, int width, int height, int stride,
                      float borderRatio = 0.06f);

// 高斯羽化(可分离卷积), 单独暴露便于测试
void featherAlpha(uint8_t* alpha, int width, int height, float sigma);

// 供测试: 计算 key 的归一化三元组与尺度
void keyTriplet(uint8_t r, uint8_t g, uint8_t b, int out[3], int* scale);

}  // namespace ck
