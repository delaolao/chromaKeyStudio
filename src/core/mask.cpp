#include "mask.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ck {

const char* maskModeName(MaskMode m) {
    switch (m) {
        case MaskMode::HoldOut: return "holdout";
        case MaskMode::FillKey: return "fillkey";
        default: return "transparent";
    }
}

const char* maskModeLabel(MaskMode m) {
    switch (m) {
        case MaskMode::HoldOut: return "强制保留";
        case MaskMode::FillKey: return "填充键色";
        default: return "强制透明";
    }
}

bool parseMaskMode(const std::string& s, MaskMode* out) {
    if (s == "transparent") { *out = MaskMode::Transparent; return true; }
    if (s == "holdout") { *out = MaskMode::HoldOut; return true; }
    if (s == "fillkey") { *out = MaskMode::FillKey; return true; }
    return false;
}

// ---------------------------------------------------------------- 栅格化

namespace {

std::vector<uint8_t>& layerFor(MaskLayers& L, MaskMode m) {
    switch (m) {
        case MaskMode::HoldOut: return L.holdOut;
        case MaskMode::FillKey: return L.fillKey;
        default: return L.transparent;
    }
}

// 扫描线填充(even-odd), 写 0/255
void fillPolygon(const std::vector<MaskPoint>& pts, int w, int h,
                 std::vector<uint8_t>& cov) {
    const size_t n = pts.size();
    if (n < 3) return;
    std::vector<float> xs;
    xs.reserve(n);
    for (int y = 0; y < h; ++y) {
        const float sy = static_cast<float>(y) + 0.5f;
        xs.clear();
        for (size_t i = 0; i < n; ++i) {
            const MaskPoint& a = pts[i];
            const MaskPoint& b = pts[(i + 1) % n];
            const float ay = a.y * h, by = b.y * h;
            if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) {
                const float t = (sy - ay) / (by - ay);
                xs.push_back(a.x * w + t * (b.x * w - a.x * w));
            }
        }
        if (xs.size() < 2) continue;
        std::sort(xs.begin(), xs.end());
        uint8_t* row = &cov[static_cast<size_t>(y) * w];
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int x0 = static_cast<int>(std::ceil(xs[k] - 0.5f));
            int x1 = static_cast<int>(std::floor(xs[k + 1] - 0.5f));
            if (x1 < 0 || x0 >= w) continue;
            x0 = std::max(0, x0);
            x1 = std::min(w - 1, x1);
            for (int x = x0; x <= x1; ++x) row[x] = 255;
        }
    }
}

// 外扩(r>0)/内缩(r<0): 可分离的 max/min 滤波
void expandCollapse(std::vector<uint8_t>& m, int w, int h, int r) {
    if (r == 0) return;
    const bool grow = r > 0;
    const int rad = std::abs(r);
    std::vector<uint8_t> tmp(m.size());

    for (int y = 0; y < h; ++y) {
        const uint8_t* src = &m[static_cast<size_t>(y) * w];
        uint8_t* dst = &tmp[static_cast<size_t>(y) * w];
        for (int x = 0; x < w; ++x) {
            const int lo = std::max(0, x - rad);
            const int hi = std::min(w - 1, x + rad);
            uint8_t v = src[lo];
            for (int i = lo + 1; i <= hi; ++i)
                v = grow ? std::max(v, src[i]) : std::min(v, src[i]);
            dst[x] = v;
        }
    }
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) {
            const int lo = std::max(0, y - rad);
            const int hi = std::min(h - 1, y + rad);
            uint8_t v = tmp[static_cast<size_t>(lo) * w + x];
            for (int i = lo + 1; i <= hi; ++i) {
                const uint8_t t = tmp[static_cast<size_t>(i) * w + x];
                v = grow ? std::max(v, t) : std::min(v, t);
            }
            m[static_cast<size_t>(y) * w + x] = v;
        }
    }
}

// 可分离盒式模糊(用于羽化, 同时把硬边变成抗锯齿的过渡)
void boxBlur(std::vector<uint8_t>& m, int w, int h, int r) {
    if (r <= 0) return;
    std::vector<uint8_t> tmp(m.size());
    const int win = 2 * r + 1;

    for (int y = 0; y < h; ++y) {
        const uint8_t* src = &m[static_cast<size_t>(y) * w];
        uint8_t* dst = &tmp[static_cast<size_t>(y) * w];
        int sum = 0;
        for (int i = -r; i <= r; ++i) sum += src[std::clamp(i, 0, w - 1)];
        for (int x = 0; x < w; ++x) {
            dst[x] = static_cast<uint8_t>(sum / win);
            const int add = std::clamp(x + r + 1, 0, w - 1);
            const int sub = std::clamp(x - r, 0, w - 1);
            sum += src[add] - src[sub];
        }
    }
    for (int x = 0; x < w; ++x) {
        int sum = 0;
        for (int i = -r; i <= r; ++i)
            sum += tmp[static_cast<size_t>(std::clamp(i, 0, h - 1)) * w + x];
        for (int y = 0; y < h; ++y) {
            m[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(sum / win);
            const int add = std::clamp(y + r + 1, 0, h - 1);
            const int sub = std::clamp(y - r, 0, h - 1);
            sum += tmp[static_cast<size_t>(add) * w + x] -
                   tmp[static_cast<size_t>(sub) * w + x];
        }
    }
}

}  // namespace

MaskLayers rasterizeMask(const std::vector<MaskPolygon>& polys, int width, int height,
                         const MaskSettings& settings) {
    MaskLayers L;
    L.width = width;
    L.height = height;
    if (width <= 0 || height <= 0) return L;

    bool need[3] = {false, false, false};
    for (const auto& p : polys) {
        if (!p.enabled || p.points.size() < 3) continue;
        need[static_cast<int>(p.mode)] = true;
    }
    if (!need[0] && !need[1] && !need[2]) return L;

    const size_t total = static_cast<size_t>(width) * height;
    for (int m = 0; m < 3; ++m)
        if (need[m]) layerFor(L, static_cast<MaskMode>(m)).assign(total, 0);

    for (const auto& p : polys) {
        if (!p.enabled || p.points.size() < 3) continue;
        fillPolygon(p.points, width, height, layerFor(L, p.mode));
    }

    const int expand = static_cast<int>(std::lround(settings.expandPx));
    const int feather = static_cast<int>(std::lround(settings.featherPx));
    for (int m = 0; m < 3; ++m) {
        if (!need[m]) continue;
        auto& layer = layerFor(L, static_cast<MaskMode>(m));
        expandCollapse(layer, width, height, expand);
        boxBlur(layer, width, height, feather);
    }
    return L;
}

void applyMask(Image& img, const MaskLayers& L, uint8_t keyR, uint8_t keyG,
               uint8_t keyB) {
    if (L.empty() || L.width != img.width || L.height != img.height) return;
    const bool hasHold = !L.holdOut.empty();
    const bool hasFill = !L.fillKey.empty();
    const bool hasTrans = !L.transparent.empty();

    for (int y = 0; y < img.height; ++y) {
        uint8_t* row = img.row(y);
        const size_t off = static_cast<size_t>(y) * L.width;
        for (int x = 0; x < img.width; ++x) {
            uint8_t* px = row + static_cast<size_t>(x) * 4;

            if (hasFill) {
                const int c = L.fillKey[off + x];
                if (c) {
                    const int k = 255 - c;
                    px[0] = static_cast<uint8_t>((px[0] * k + keyR * c) / 255);
                    px[1] = static_cast<uint8_t>((px[1] * k + keyG * c) / 255);
                    px[2] = static_cast<uint8_t>((px[2] * k + keyB * c) / 255);
                }
            }
            if (hasHold) {
                const int c = L.holdOut[off + x];
                if (c > px[3]) px[3] = static_cast<uint8_t>(c);
            }
            if (hasTrans) {
                const int c = L.transparent[off + x];
                if (c) px[3] = static_cast<uint8_t>(px[3] * (255 - c) / 255);
            }
        }
    }
}

// ---------------------------------------------------------------- JSON

namespace {

void appendNum(std::string& s, double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    s += buf;
}

}  // namespace

std::string maskToJson(const std::vector<MaskPolygon>& polys, const MaskSettings& s) {
    std::string out = "{\n  \"version\": 1,\n  \"feather\": ";
    appendNum(out, s.featherPx);
    out += ",\n  \"expand\": ";
    appendNum(out, s.expandPx);
    out += ",\n  \"polygons\": [";
    bool first = true;
    for (const auto& p : polys) {
        if (p.points.size() < 3) continue;
        out += first ? "\n" : ",\n";
        first = false;
        out += "    { \"mode\": \"";
        out += maskModeName(p.mode);
        out += "\", \"enabled\": ";
        out += p.enabled ? "true" : "false";
        out += ", \"points\": [";
        for (size_t i = 0; i < p.points.size(); ++i) {
            if (i) out += ", ";
            out += "[";
            appendNum(out, p.points[i].x);
            out += ", ";
            appendNum(out, p.points[i].y);
            out += "]";
        }
        out += "] }";
    }
    out += first ? "]\n}\n" : "\n  ]\n}\n";
    return out;
}

namespace {

struct JScan {
    const char* p;
    const char* e;

    void ws() {
        while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
    }
    bool eat(char c) {
        ws();
        if (p < e && *p == c) { ++p; return true; }
        return false;
    }
    bool str(std::string* out) {
        ws();
        if (p >= e || *p != '"') return false;
        ++p;
        out->clear();
        while (p < e && *p != '"') {
            if (*p == '\\' && p + 1 < e) {
                ++p;
                switch (*p) {
                    case 'n': out->push_back('\n'); break;
                    case 't': out->push_back('\t'); break;
                    default: out->push_back(*p); break;
                }
                ++p;
            } else {
                out->push_back(*p++);
            }
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
    bool num(double* out) {
        ws();
        char* end = nullptr;
        const double v = std::strtod(p, &end);
        if (end == p) return false;
        p = end;
        *out = v;
        return true;
    }
    bool boolean(bool* out) {
        ws();
        if (e - p >= 4 && !std::strncmp(p, "true", 4)) { p += 4; *out = true; return true; }
        if (e - p >= 5 && !std::strncmp(p, "false", 5)) { p += 5; *out = false; return true; }
        return false;
    }
    // 跳过任意值(用于未知字段)
    bool skip() {
        ws();
        if (p >= e) return false;
        if (*p == '"') { std::string tmp; return str(&tmp); }
        if (*p == '[' || *p == '{') {
            const char open = *p, close = (open == '[') ? ']' : '}';
            int depth = 0;
            while (p < e) {
                if (*p == '"') { std::string tmp; if (!str(&tmp)) return false; continue; }
                if (*p == open) ++depth;
                else if (*p == close) { --depth; ++p; if (!depth) return true; continue; }
                ++p;
            }
            return false;
        }
        double d;
        if (num(&d)) return true;
        bool b;
        return boolean(&b);
    }
};

bool parsePolygon(JScan& s, MaskPolygon* out) {
    if (!s.eat('{')) return false;
    out->points.clear();
    out->mode = MaskMode::Transparent;
    out->enabled = true;
    while (true) {
        std::string key;
        if (!s.str(&key)) return false;
        if (!s.eat(':')) return false;
        if (key == "mode") {
            std::string v;
            if (!s.str(&v)) return false;
            parseMaskMode(v, &out->mode);
        } else if (key == "enabled") {
            bool b = true;
            if (!s.boolean(&b)) return false;
            out->enabled = b;
        } else if (key == "points") {
            if (!s.eat('[')) return false;
            while (!s.eat(']')) {
                if (!s.eat('[')) return false;
                double x = 0, y = 0;
                if (!s.num(&x)) return false;
                if (!s.eat(',')) return false;
                if (!s.num(&y)) return false;
                if (!s.eat(']')) return false;
                out->points.push_back(
                    {static_cast<float>(x), static_cast<float>(y)});
                s.ws();
                if (s.p < s.e && *s.p == ',') ++s.p;
            }
        } else {
            if (!s.skip()) return false;
        }
        if (s.eat(',')) continue;
        if (s.eat('}')) break;
        return false;
    }
    return out->points.size() >= 3;
}

}  // namespace

bool maskFromJson(const std::string& text, std::vector<MaskPolygon>* polys,
                  MaskSettings* settings) {
    if (!polys || !settings) return false;
    // 跳过 UTF-8 BOM: Windows 记事本 / PowerShell 存的文件常带
    const char* begin = text.data();
    size_t len = text.size();
    if (len >= 3 && static_cast<unsigned char>(begin[0]) == 0xEF &&
        static_cast<unsigned char>(begin[1]) == 0xBB &&
        static_cast<unsigned char>(begin[2]) == 0xBF) {
        begin += 3;
        len -= 3;
    }
    JScan s{begin, begin + len};
    if (!s.eat('{')) return false;
    polys->clear();
    while (true) {
        std::string key;
        if (!s.str(&key)) return false;
        if (!s.eat(':')) return false;
        if (key == "feather") {
            double v = 0;
            if (!s.num(&v)) return false;
            settings->featherPx = static_cast<float>(v);
        } else if (key == "expand") {
            double v = 0;
            if (!s.num(&v)) return false;
            settings->expandPx = static_cast<float>(v);
        } else if (key == "polygons") {
            if (!s.eat('[')) return false;
            while (!s.eat(']')) {
                MaskPolygon p;
                if (!parsePolygon(s, &p)) return false;
                polys->push_back(std::move(p));
                s.ws();
                if (s.p < s.e && *s.p == ',') ++s.p;
            }
        } else {
            if (!s.skip()) return false;
        }
        if (s.eat(',')) continue;
        if (s.eat('}')) break;
        return false;
    }
    return true;
}

std::string defaultMaskPath(const std::string& videoPath) {
    std::string base = videoPath;
    const size_t slash = base.find_last_of("/\\");
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        base.erase(dot);
    return base + ".ckmask.json";
}

}  // namespace ck
