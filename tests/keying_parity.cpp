// 键控对拍工具: 读一张 RGB 原始帧 + 参数, 输出 RGBA 原始帧。
// 用途是拿 Python 版的结果逐像素比对, 确认 C++ 重写没有改坏画质。
//
// 用法: keying_parity <in.rgb> <out.rgba> <w> <h> <keyR> <keyG> <keyB>
//                      <similarity> <blend> <feather> <despill> <despillMix>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/keying.h"

int main(int argc, char** argv) {
    if (argc < 13) {
        std::fprintf(stderr,
                     "usage: %s in.rgb out.rgba w h keyR keyG keyB sim blend "
                     "feather despill despillMix\n",
                     argv[0]);
        return 2;
    }
    const char* inPath = argv[1];
    const char* outPath = argv[2];
    const int w = std::atoi(argv[3]);
    const int h = std::atoi(argv[4]);

    ck::KeyParams p;
    p.keyR = static_cast<uint8_t>(std::atoi(argv[5]));
    p.keyG = static_cast<uint8_t>(std::atoi(argv[6]));
    p.keyB = static_cast<uint8_t>(std::atoi(argv[7]));
    p.similarity = static_cast<float>(std::atof(argv[8]));
    p.blend = static_cast<float>(std::atof(argv[9]));
    p.featherSigma = static_cast<float>(std::atof(argv[10]));
    p.despill = std::atoi(argv[11]) != 0;
    p.despillMix = static_cast<float>(std::atof(argv[12]));
    p.spillMatte = true;
    p.neutralize = true;

    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    FILE* f = std::fopen(inPath, "rb");
    if (!f) { std::perror("open in"); return 1; }
    if (std::fread(rgb.data(), 1, rgb.size(), f) != rgb.size()) {
        std::fprintf(stderr, "输入尺寸不符\n");
        std::fclose(f);
        return 1;
    }
    std::fclose(f);

    ck::Image out;
    ck::keyImage(rgb.data(), w, h, w * 3, p, out);

    f = std::fopen(outPath, "wb");
    if (!f) { std::perror("open out"); return 1; }
    std::fwrite(out.data.data(), 1, out.data.size(), f);
    std::fclose(f);

    // 顺便打印统计, 便于肉眼对比
    size_t transparent = 0, semi = 0;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = out.row(y);
        for (int x = 0; x < w; ++x) {
            const uint8_t a = row[x * 4 + 3];
            if (a <= 5) transparent++;
            else if (a < 250) semi++;
        }
    }
    std::printf("w=%d h=%d transparent=%.2f%% semi=%.2f%%\n", w, h,
                100.0 * transparent / (w * h), 100.0 * semi / (w * h));
    return 0;
}
