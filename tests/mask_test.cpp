// 掩码核心自测: 栅格化 / 三种模式 / 外扩 / 羽化 / JSON 往返
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

#include "core/keying.h"
#include "core/mask.h"

namespace {

int g_failed = 0;

void check(bool cond, const char* what) {
    std::printf("  [%s] %s\n", cond ? "ok" : "FAIL", what);
    if (!cond) ++g_failed;
}

ck::Image makeOpaque(int w, int h) {
    ck::Image img(w, h);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = img.row(y);
        for (int x = 0; x < w; ++x) {
            row[x * 4 + 0] = 200;
            row[x * 4 + 1] = 100;
            row[x * 4 + 2] = 50;
            row[x * 4 + 3] = 255;
        }
    }
    return img;
}

uint8_t alphaAt(const ck::Image& img, int x, int y) {
    return img.row(y)[x * 4 + 3];
}

}  // namespace

int main() {
    const int W = 320, H = 180;

    // ---- 1. 顶部一条 10% 的透明带
    {
        std::vector<ck::MaskPolygon> polys(1);
        polys[0].mode = ck::MaskMode::Transparent;
        polys[0].points = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 0.1f}, {0.f, 0.1f}};
        ck::MaskSettings s;
        s.featherPx = 0;
        s.expandPx = 0;
        auto layers = ck::rasterizeMask(polys, W, H, s);
        check(!layers.empty(), "掩码非空");

        ck::Image img = makeOpaque(W, H);
        ck::applyMask(img, layers, 0, 177, 64);
        check(alphaAt(img, 10, 5) == 0, "带内 alpha=0");
        check(alphaAt(img, W / 2, 0) == 0, "顶边 alpha=0");
        check(alphaAt(img, W / 2, H - 5) == 255, "带外 alpha=255");
        int op = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (alphaAt(img, x, y) > 127) ++op;
        const double ratio = double(op) / (W * H);
        check(std::abs(ratio - 0.9) < 0.02, "透明比例约 90%");
    }

    // ---- 2. 多块多边形 + 强制保留
    {
        std::vector<ck::MaskPolygon> polys(2);
        polys[0].mode = ck::MaskMode::Transparent;
        polys[0].points = {{0.f, 0.f}, {0.5f, 0.f}, {0.5f, 0.2f}, {0.f, 0.2f}};
        polys[1].mode = ck::MaskMode::HoldOut;
        polys[1].points = {{0.5f, 0.f}, {1.f, 0.f}, {1.f, 0.2f}, {0.5f, 0.2f}};
        ck::MaskSettings s;
        s.featherPx = 0;
        auto layers = ck::rasterizeMask(polys, W, H, s);
        ck::Image img = makeOpaque(W, H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) img.row(y)[x * 4 + 3] = 0;  // 先全透明
        ck::applyMask(img, layers, 0, 177, 64);
        check(alphaAt(img, W / 4, 10) == 0, "左半(强制透明) alpha=0");
        check(alphaAt(img, 3 * W / 4, 10) == 255, "右半(强制保留) alpha=255");
    }

    // ---- 3. 填充键色
    {
        std::vector<ck::MaskPolygon> polys(1);
        polys[0].mode = ck::MaskMode::FillKey;
        polys[0].points = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
        ck::MaskSettings s;
        s.featherPx = 0;
        auto layers = ck::rasterizeMask(polys, W, H, s);
        ck::Image img = makeOpaque(W, H);
        ck::applyMask(img, layers, 0, 177, 64);
        check(img.row(10)[10 * 4 + 1] == 177, "键色已填入(G=177)");
        check(img.row(10)[10 * 4 + 0] == 0, "键色已填入(R=0)");
    }

    // ---- 4. 外扩: 半径 5 应让覆盖面积变大
    {
        std::vector<ck::MaskPolygon> polys(1);
        polys[0].points = {{0.4f, 0.4f}, {0.6f, 0.4f}, {0.6f, 0.6f}, {0.4f, 0.6f}};
        ck::MaskSettings s0;
        s0.featherPx = 0;
        s0.expandPx = 0;
        auto a = ck::rasterizeMask(polys, W, H, s0);
        ck::MaskSettings s1 = s0;
        s1.expandPx = 5;
        auto b = ck::rasterizeMask(polys, W, H, s1);
        auto count = [](const std::vector<uint8_t>& v) {
            int n = 0;
            for (auto c : v) if (c > 127) ++n;
            return n;
        };
        check(count(b.transparent) > count(a.transparent), "外扩后覆盖变大");
    }

    // ---- 5. 羽化: 边缘应出现 0<alpha<255 的过渡像素
    {
        std::vector<ck::MaskPolygon> polys(1);
        polys[0].points = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 0.5f}, {0.f, 0.5f}};
        ck::MaskSettings s;
        s.featherPx = 4;
        auto layers = ck::rasterizeMask(polys, W, H, s);
        int soft = 0;
        for (auto c : layers.transparent)
            if (c > 10 && c < 245) ++soft;
        check(soft > 100, "羽化产生了过渡像素");
    }

    // ---- 6. JSON 往返
    {
        std::vector<ck::MaskPolygon> polys(2);
        polys[0].mode = ck::MaskMode::Transparent;
        polys[0].points = {{0.1f, 0.2f}, {0.3f, 0.2f}, {0.3f, 0.4f}};
        polys[1].mode = ck::MaskMode::FillKey;
        polys[1].enabled = false;
        polys[1].points = {{0.5f, 0.5f}, {0.7f, 0.5f}, {0.7f, 0.7f}, {0.5f, 0.7f}};
        ck::MaskSettings s;
        s.featherPx = 2.5f;
        s.expandPx = -1.5f;

        const std::string json = ck::maskToJson(polys, s);
        std::vector<ck::MaskPolygon> back;
        ck::MaskSettings s2;
        const bool ok = ck::maskFromJson(json, &back, &s2);
        check(ok, "JSON 解析成功");
        check(back.size() == 2, "多边形数量一致");
        check(back.size() == 2 && back[0].mode == ck::MaskMode::Transparent &&
                  back[0].points.size() == 3,
              "第一块内容一致");
        check(back.size() == 2 && back[1].mode == ck::MaskMode::FillKey &&
                  !back[1].enabled,
              "第二块模式/开关一致");
        check(std::abs(s2.featherPx - 2.5f) < 0.01f &&
                  std::abs(s2.expandPx + 1.5f) < 0.01f,
              "参数往返一致");
        std::printf("  样例 JSON 前 80 字节: %.80s\n", json.c_str());

        // 带 BOM 也要能读
        std::string withBom = "\xEF\xBB\xBF" + json;
        std::vector<ck::MaskPolygon> b2;
        ck::MaskSettings s3;
        check(ck::maskFromJson(withBom, &b2, &s3) && b2.size() == 2,
              "带 UTF-8 BOM 的 JSON 也能解析");
    }

    std::printf("\n%s\n", g_failed == 0 ? "结论: 全部通过" : "结论: 有失败项");
    return g_failed == 0 ? 0 : 1;
}
