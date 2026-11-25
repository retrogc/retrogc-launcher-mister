#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include "theme.h"

namespace {

// Premultiplied float pixel, so transparent edges don't bleed dark when resampling.
struct PxF {
    float r, g, b, a;
};

std::vector<PxF> to_premul(const Image &img)
{
    std::vector<PxF> out(img.px.size());
    for (size_t i = 0; i < img.px.size(); i++) {
        uint32_t p = img.px[i];
        float a = (p >> 24) / 255.0f;
        out[i] = { ((p >> 16) & 0xFF) * a, ((p >> 8) & 0xFF) * a, (p & 0xFF) * a, a };
    }
    return out;
}

uint32_t from_premul(const PxF &p)
{
    if (p.a <= 0.0001f) return 0;
    auto ch = [&](float v) { return (uint32_t)std::clamp((int)lroundf(v / p.a), 0, 255); };
    uint32_t a = (uint32_t)std::clamp((int)lroundf(p.a * 255.0f), 0, 255);
    return (a << 24) | (ch(p.r) << 16) | (ch(p.g) << 8) | ch(p.b);
}

// One axis of a separable resample: |n_src| samples -> |n_dst| samples, |stride| apart.
void resample_axis(const PxF *src, int n_src, PxF *dst, int n_dst, int src_step, int dst_step)
{
    const float scale = (float)n_src / n_dst;

    if (scale > 1.0f) {
        // Shrinking: average the source interval each destination sample covers.
        for (int i = 0; i < n_dst; i++) {
            float x0 = i * scale, x1 = x0 + scale;
            PxF acc = { 0, 0, 0, 0 };
            float wsum = 0;
            for (int x = (int)x0; x < (int)ceilf(x1) && x < n_src; x++) {
                float w = std::min(x1, (float)x + 1) - std::max(x0, (float)x);
                if (w <= 0) continue;
                const PxF &s = src[x * src_step];
                acc.r += s.r * w;
                acc.g += s.g * w;
                acc.b += s.b * w;
                acc.a += s.a * w;
                wsum += w;
            }
            if (wsum > 0) acc = { acc.r / wsum, acc.g / wsum, acc.b / wsum, acc.a / wsum };
            dst[i * dst_step] = acc;
        }
    } else {
        // Growing: bilinear.
        for (int i = 0; i < n_dst; i++) {
            float x = (i + 0.5f) * scale - 0.5f;
            int x0 = (int)floorf(x);
            float t = x - x0;
            int a = std::clamp(x0, 0, n_src - 1), b = std::clamp(x0 + 1, 0, n_src - 1);
            const PxF &p = src[a * src_step], &q = src[b * src_step];
            dst[i * dst_step] = { p.r + (q.r - p.r) * t, p.g + (q.g - p.g) * t, p.b + (q.b - p.b) * t,
                                  p.a + (q.a - p.a) * t };
        }
    }
}

uint32_t darken(uint32_t p, float f)
{
    auto ch = [&](int shift) { return (uint32_t)(((p >> shift) & 0xFF) * f) << shift; };
    return (p & 0xFF000000u) | ch(16) | ch(8) | ch(0);
}

} // namespace

bool image_load(const std::string &path, Image &out, std::string &error)
{
    int w, h, n;
    unsigned char *data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) {
        error = stbi_failure_reason() ? stbi_failure_reason() : "unknown error";
        return false;
    }

    out.w = w;
    out.h = h;
    out.px.resize((size_t)w * h);
    for (size_t i = 0; i < out.px.size(); i++) {
        const unsigned char *s = data + i * 4;
        out.px[i] = ((uint32_t)s[3] << 24) | ((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2];
    }
    stbi_image_free(data);
    return true;
}

Image image_resize(const Image &src, int w, int h)
{
    Image out;
    if (src.empty() || w <= 0 || h <= 0) return out;

    std::vector<PxF> in = to_premul(src);

    // Horizontal pass: src.w x src.h -> w x src.h
    std::vector<PxF> mid((size_t)w * src.h);
    for (int y = 0; y < src.h; y++)
        resample_axis(&in[(size_t)y * src.w], src.w, &mid[(size_t)y * w], w, 1, 1);

    // Vertical pass: w x src.h -> w x h
    std::vector<PxF> fin((size_t)w * h);
    for (int x = 0; x < w; x++) resample_axis(&mid[x], src.h, &fin[x], h, w, w);

    out.w = w;
    out.h = h;
    out.px.resize(fin.size());
    for (size_t i = 0; i < fin.size(); i++) out.px[i] = from_premul(fin[i]);
    return out;
}

Image image_fit(const Image &src, int w, int h)
{
    if (src.empty()) return {};
    float s = std::min((float)w / src.w, (float)h / src.h);
    return image_resize(src, std::max(1, (int)lroundf(src.w * s)), std::max(1, (int)lroundf(src.h * s)));
}

Image image_fill(const Image &src, int w, int h)
{
    if (src.empty()) return {};

    // Crop the source to the target aspect ratio, then resize.
    float target = (float)w / h, aspect = (float)src.w / src.h;
    int cw = src.w, ch = src.h;
    if (aspect > target) cw = std::max(1, (int)lroundf(src.h * target));
    else ch = std::max(1, (int)lroundf(src.w / target));

    Image crop;
    crop.w = cw;
    crop.h = ch;
    crop.px.resize((size_t)cw * ch);
    int ox = (src.w - cw) / 2, oy = (src.h - ch) / 2;
    for (int y = 0; y < ch; y++)
        memcpy(&crop.px[(size_t)y * cw], &src.px[(size_t)(y + oy) * src.w + ox], (size_t)cw * 4);

    return image_resize(crop, w, h);
}

void image_blur(Image &img, int radius)
{
    if (img.empty() || radius <= 0) return;

    std::vector<PxF> buf = to_premul(img), tmp(buf.size());
    const int w = img.w, h = img.h;

    auto box = [&](const std::vector<PxF> &src, std::vector<PxF> &dst, bool horizontal) {
        int n = horizontal ? w : h, lines = horizontal ? h : w;
        int step = horizontal ? 1 : w;
        float norm = 1.0f / (2 * radius + 1);
        for (int l = 0; l < lines; l++) {
            const PxF *s = &src[horizontal ? (size_t)l * w : (size_t)l];
            PxF *d = &dst[horizontal ? (size_t)l * w : (size_t)l];
            PxF acc = { 0, 0, 0, 0 };
            for (int k = -radius; k <= radius; k++) {
                const PxF &p = s[std::clamp(k, 0, n - 1) * step];
                acc.r += p.r, acc.g += p.g, acc.b += p.b, acc.a += p.a;
            }
            for (int i = 0; i < n; i++) {
                d[i * step] = { acc.r * norm, acc.g * norm, acc.b * norm, acc.a * norm };
                const PxF &out = s[std::clamp(i - radius, 0, n - 1) * step];
                const PxF &in = s[std::clamp(i + radius + 1, 0, n - 1) * step];
                acc.r += in.r - out.r, acc.g += in.g - out.g, acc.b += in.b - out.b, acc.a += in.a - out.a;
            }
        }
    };

    for (int pass = 0; pass < 3; pass++) {
        box(buf, tmp, true);
        box(tmp, buf, false);
    }

    for (size_t i = 0; i < buf.size(); i++) img.px[i] = from_premul(buf[i]);
}

Image image_ambient(const Image *cover, int w, int h, bool scanlines)
{
    Image out;
    out.w = w;
    out.h = h;
    out.px.assign((size_t)w * h, 0xFF000000u | theme::Bg);

    if (cover && !cover->empty()) {
        // Blur at 1/8 size (cheap, and the blur hides the upscale), like the PC's blur(60).
        int sw = std::max(4, w / 8), sh = std::max(4, h / 8);
        Image small = image_fill(*cover, sw, sh);
        image_blur(small, 4);
        Image big = image_resize(small, w, h);

        // Cover at 30% opacity, then the #B3120E24 veil (70%) on top.
        const uint32_t bg = theme::Bg;
        const float br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
        for (size_t i = 0; i < out.px.size(); i++) {
            uint32_t p = big.px[i];
            float a = (p >> 24) / 255.0f * 0.3f;
            float r = br + (((p >> 16) & 0xFF) - br) * a;
            float g = bgc + (((p >> 8) & 0xFF) - bgc) * a;
            float b = bb + ((p & 0xFF) - bb) * a;
            r = r * 0.3f + br * 0.7f;
            g = g * 0.3f + bgc * 0.7f;
            b = b * 0.3f + bb * 0.7f;
            out.px[i] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    if (scanlines) {
        // One dark line every 3 pixels at 30% (PixelPerfect.CreateScanlines).
        for (int y = 2; y < h; y += 3)
            for (int x = 0; x < w; x++) out.px[(size_t)y * w + x] = darken(out.px[(size_t)y * w + x], 0.7f);
    }

    return out;
}

lv_draw_buf_t *image_to_draw_buf(const Image &img, bool opaque)
{
    if (img.empty()) return nullptr;

    lv_draw_buf_t *buf = lv_draw_buf_create(img.w, img.h, opaque ? LV_COLOR_FORMAT_XRGB8888 : LV_COLOR_FORMAT_ARGB8888,
                                            LV_STRIDE_AUTO);
    if (!buf) return nullptr;

    for (int y = 0; y < img.h; y++)
        memcpy(buf->data + (size_t)y * buf->header.stride, &img.px[(size_t)y * img.w], (size_t)img.w * 4);
    return buf;
}

bool image_save_png(const std::string &path, const uint8_t *argb, int w, int h, int stride)
{
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        const uint8_t *s = argb + (size_t)y * stride;
        uint8_t *d = &rgba[(size_t)y * w * 4];
        for (int x = 0; x < w; x++) {
            // memory order B, G, R, A
            d[x * 4 + 0] = s[x * 4 + 2];
            d[x * 4 + 1] = s[x * 4 + 1];
            d[x * 4 + 2] = s[x * 4 + 0];
            d[x * 4 + 3] = 0xFF;
        }
    }
    return stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
}
