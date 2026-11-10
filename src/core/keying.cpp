#include "keying.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ck {
namespace {

constexpr int kLumaFloor = 64;   // 与 Python 版一致
constexpr float kNormScale = 64.0f;
constexpr int kTransparentMax = 5;

inline int luma8(int r, int g, int b) {
    // 与 OpenCV cvtColor(RGB2GRAY) 一致的 BT.601 权重 + 四舍五入
    return (299 * r + 587 * g + 114 * b + 500) / 1000;
}

inline uint8_t normalized(int c, int luma) {
    const int denom = luma > kLumaFloor ? luma : kLumaFloor;
    int v = (c * static_cast<int>(kNormScale) + denom / 2) / denom;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return static_cast<uint8_t>(v);
}

// 溢出量: 主通道超出另两通道最大值的部分
inline int spillExcess(int r, int g, int b, int mainChannel) {
    if (mainChannel == 1) return std::max(0, g - std::max(r, b));
    if (mainChannel == 2) return std::max(0, b - std::max(r, g));
    return std::max(0, r - std::max(g, b));
}

}  // namespace

void Image::resize(int w, int h) {
    width = w;
    height = h;
    stride = w * 4;
    data.assign(static_cast<size_t>(stride) * h, 0);
}

void keyTriplet(uint8_t r, uint8_t g, uint8_t b, int out[3], int* scale) {
    const int luma = luma8(r, g, b);
    out[0] = normalized(r, luma);
    out[1] = normalized(g, luma);
    out[2] = normalized(b, luma);
    *scale = std::abs(out[0] - kLumaFloor) + std::abs(out[1] - kLumaFloor) +
             std::abs(out[2] - kLumaFloor);
    if (*scale < 8) *scale = 8;
}

void featherAlpha(uint8_t* alpha, int width, int height, float sigma) {
    if (sigma <= 0.0f || width <= 0 || height <= 0) return;
    // 三倍标准差截断, 与 OpenCV GaussianBlur 的核大小策略一致
    const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0f)));
    std::vector<float> kernel(2 * radius + 1);
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float v = std::exp(-(i * i) / (2.0f * sigma * sigma));
        kernel[i + radius] = v;
        sum += v;
    }
    for (auto& v : kernel) v /= sum;

    std::vector<float> tmp(static_cast<size_t>(width) * height);
    // 横向
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = alpha + static_cast<size_t>(y) * width;
        float* dst = tmp.data() + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                int xx = x + k;
                if (xx < 0) xx = 0;
                if (xx >= width) xx = width - 1;
                acc += src[xx] * kernel[k + radius];
            }
            dst[x] = acc;
        }
    }
    // 纵向
    for (int y = 0; y < height; ++y) {
        uint8_t* dst = alpha + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                int yy = y + k;
                if (yy < 0) yy = 0;
                if (yy >= height) yy = height - 1;
                acc += tmp[static_cast<size_t>(yy) * width + x] * kernel[k + radius];
            }
            const int v = static_cast<int>(acc + 0.5f);
            dst[x] = static_cast<uint8_t>(std::clamp(v, 0, 255));
        }
    }
}

void keyImage(const uint8_t* rgb, int width, int height, int rgbStride,
              const KeyParams& p, Image& out) {
    out.resize(width, height);

    int knd[3];
    int scale = 0;
    keyTriplet(p.keyG == 0 && p.keyB == 0 && p.keyR == 0 ? 0 : p.keyR,
               p.keyG, p.keyB, knd, &scale);

    // 主通道 = 键色里最大的那个通道(绿幕是 G, 蓝幕是 B)
    int mainChannel = 1;
    if (p.keyB >= p.keyG && p.keyB >= p.keyR) mainChannel = 2;
    else if (p.keyR >= p.keyG && p.keyR >= p.keyB) mainChannel = 0;

    int excessBg = 0;
    if (mainChannel == 1) excessBg = p.keyG - std::max(p.keyR, p.keyB);
    else if (mainChannel == 2) excessBg = p.keyB - std::max(p.keyR, p.keyG);
    else excessBg = p.keyR - std::max(p.keyG, p.keyB);
    if (excessBg < 1) excessBg = 1;

    // alpha 查找表: 距离 -> alpha
    uint8_t lut[256];
    const float width_ = std::max(p.blend, 1e-4f);
    for (int d = 0; d < 256; ++d) {
        const float dn = static_cast<float>(d) / static_cast<float>(scale);
        float a = (dn - p.similarity) / width_;
        a = std::clamp(a, 0.0f, 1.0f);
        lut[d] = static_cast<uint8_t>(a * 255.0f + 0.5f);
    }
    // 溢色覆盖率查找表: excess -> 1 - excess/excessBg
    uint8_t spillLut[256];
    for (int e = 0; e < 256; ++e) {
        float a = 1.0f - static_cast<float>(e) / static_cast<float>(excessBg);
        a = std::clamp(a, 0.0f, 1.0f);
        spillLut[e] = static_cast<uint8_t>(a * 255.0f + 0.5f);
    }

    std::vector<uint8_t> alpha(static_cast<size_t>(width) * height);

    for (int y = 0; y < height; ++y) {
        const uint8_t* src = rgb + static_cast<size_t>(y) * rgbStride;
        uint8_t* dst = out.row(y);
        uint8_t* arow = alpha.data() + static_cast<size_t>(y) * width;
        for (int x = 0; x < width; ++x, src += 3, dst += 4) {
            const int r = src[0], g = src[1], b = src[2];
            const int luma = luma8(r, g, b);
            const int nr = normalized(r, luma);
            const int ng = normalized(g, luma);
            const int nb = normalized(b, luma);

            int d = std::abs(nr - knd[0]) + std::abs(ng - knd[1]) +
                    std::abs(nb - knd[2]);
            if (d > 255) d = 255;
            uint8_t a = lut[d];

            const int excess = spillExcess(r, g, b, mainChannel);
            if (p.spillMatte && excessBg > 4) {
                a = std::min(a, spillLut[excess]);
            }

            int orr = r, og = g, ob = b;
            if (p.despill && excess > 0) {
                const int cut = static_cast<int>(excess * p.despillMix + 0.5f);
                if (mainChannel == 1) og = std::max(0, g - cut);
                else if (mainChannel == 2) ob = std::max(0, b - cut);
                else orr = std::max(0, r - cut);
            }

            dst[0] = static_cast<uint8_t>(orr);
            dst[1] = static_cast<uint8_t>(og);
            dst[2] = static_cast<uint8_t>(ob);
            dst[3] = a;
            arow[x] = a;
        }
    }

    if (p.featherSigma > 0.0f) {
        featherAlpha(alpha.data(), width, height, p.featherSigma);
        for (int y = 0; y < height; ++y) {
            uint8_t* dst = out.row(y);
            const uint8_t* arow = alpha.data() + static_cast<size_t>(y) * width;
            for (int x = 0; x < width; ++x) dst[x * 4 + 3] = arow[x];
        }
    }

    if (p.neutralize) {
        for (int y = 0; y < height; ++y) {
            uint8_t* dst = out.row(y);
            for (int x = 0; x < width; ++x, dst += 4) {
                if (dst[3] <= kTransparentMax) {
                    dst[0] = dst[1] = dst[2] = 0;
                }
            }
        }
    }
}

Suggestion suggestKey(const uint8_t* rgb, int width, int height, int stride,
                      float borderRatio) {
    Suggestion s;
    const int b = std::max(3, static_cast<int>(
        std::min(width, height) * borderRatio));
    std::vector<int> histR(256, 0), histG(256, 0), histB(256, 0);
    int count = 0;

    auto collect = [&](int x, int y) {
        const uint8_t* p = rgb + static_cast<size_t>(y) * stride + x * 3;
        histR[p[0]]++;
        histG[p[1]]++;
        histB[p[2]]++;
        count++;
    };
    for (int y = 0; y < height; ++y) {
        const bool edgeRow = (y < b) || (y >= height - b);
        if (edgeRow) {
            for (int x = 0; x < width; ++x) collect(x, y);
        } else {
            for (int x = 0; x < b && x < width; ++x) collect(x, y);
            for (int x = std::max(0, width - b); x < width; ++x) collect(x, y);
        }
    }
    if (count == 0) return s;

    auto median = [&](std::vector<int>& hist) {
        int half = count / 2, acc = 0;
        for (int i = 0; i < 256; ++i) {
            acc += hist[i];
            if (acc > half) return i;
        }
        return 0;
    };
    s.r = static_cast<uint8_t>(median(histR));
    s.g = static_cast<uint8_t>(median(histG));
    s.b = static_cast<uint8_t>(median(histB));

    // 用同一套距离度量, 取边缘像素距离的 95 分位
    int knd[3], scale;
    keyTriplet(s.r, s.g, s.b, knd, &scale);
    std::vector<int> distHist(256, 0);
    auto measure = [&](int x, int y) {
        const uint8_t* p = rgb + static_cast<size_t>(y) * stride + x * 3;
        const int luma = luma8(p[0], p[1], p[2]);
        int d = std::abs(normalized(p[0], luma) - knd[0]) +
                std::abs(normalized(p[1], luma) - knd[1]) +
                std::abs(normalized(p[2], luma) - knd[2]);
        distHist[std::min(d, 255)]++;
    };
    for (int y = 0; y < height; ++y) {
        const bool edgeRow = (y < b) || (y >= height - b);
        if (edgeRow) {
            for (int x = 0; x < width; ++x) measure(x, y);
        } else {
            for (int x = 0; x < b && x < width; ++x) measure(x, y);
            for (int x = std::max(0, width - b); x < width; ++x) measure(x, y);
        }
    }
    int acc = 0, p95 = 0;
    for (int i = 0; i < 256; ++i) {
        acc += distHist[i];
        if (acc >= static_cast<int>(count * 0.95f)) { p95 = i; break; }
    }
    s.similarity = static_cast<float>(p95) / static_cast<float>(scale) + 0.03f;
    s.similarity = std::clamp(s.similarity, 0.05f, 0.95f);
    return s;
}

}  // namespace ck
