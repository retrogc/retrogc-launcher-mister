// Cover art helpers: decode once, resample to the exact size it is shown at, and build the
// blurred ambient background. Done once per card, so drawing is a plain blit afterwards
// (LVGL scaling an image at every redraw would be far too slow on the Cortex-A9).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "lvgl.h"

struct Image {
    int w = 0;
    int h = 0;
    std::vector<uint32_t> px; // 0xAARRGGBB, straight alpha (LVGL ARGB8888 layout)

    bool empty() const { return w <= 0 || h <= 0; }
};

// PNG / JPEG / BMP / GIF (first frame) via stb_image.
bool image_load(const std::string &path, Image &out, std::string &error);

// Resamples |src| to exactly w x h (area average when shrinking, bilinear when growing).
Image image_resize(const Image &src, int w, int h);

// Largest size that fits w x h keeping the aspect ratio ("Uniform").
Image image_fit(const Image &src, int w, int h);

// Fills w x h keeping the aspect ratio, cropping the overflow ("UniformToFill").
Image image_fill(const Image &src, int w, int h);

// In-place blur (3 box passes ~ gaussian).
void image_blur(Image &img, int radius);

// The ambient background of the main screen at w x h: |cover| (may be empty) blurred under a
// dark veil, plus optional scanlines. Opaque.
Image image_ambient(const Image *cover, int w, int h, bool scanlines);

// Copies |img| into a new LVGL draw buffer (caller owns it: lv_draw_buf_destroy).
// |opaque| uses XRGB8888, which LVGL blits without blending.
lv_draw_buf_t *image_to_draw_buf(const Image &img, bool opaque);

// Writes the image as PNG (screenshots).
bool image_save_png(const std::string &path, const uint8_t *argb, int w, int h, int stride);
