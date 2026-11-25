#include "ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>

#include "theme.h"

extern "C" {
extern const unsigned char font_press_start_2p[];
extern const unsigned int font_press_start_2p_len;
extern const unsigned char font_vt323[];
extern const unsigned int font_vt323_len;
extern const unsigned char font_inter[];
extern const unsigned int font_inter_len;
}

namespace {

enum FontId { PixelFont = 0, MonoFont = 1, BodyFont = 2 };

std::string upper(std::string s)
{
    for (auto &c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

void set_text(lv_obj_t *label, const std::string &text)
{
    const char *cur = lv_label_get_text(label);
    if (!cur || text != cur) lv_label_set_text(label, text.c_str());
}

void show(lv_obj_t *obj, bool visible)
{
    if (!obj) return;
    if (visible) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

void fill(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_bg_color(obj, theme::rgb(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
}

void border(lv_obj_t *obj, uint32_t color, int width)
{
    lv_obj_set_style_border_color(obj, theme::rgb(color), 0);
    lv_obj_set_style_border_width(obj, width, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
}

void free_buf(lv_draw_buf_t *&buf)
{
    if (!buf) return;
    lv_image_cache_drop(buf);
    lv_draw_buf_destroy(buf);
    buf = nullptr;
}

// Darkens every 3rd row (the PC's scanline overlay, baked into the cover image).
void bake_scanlines(Image &img, float keep)
{
    for (int y = 2; y < img.h; y += 3) {
        for (int x = 0; x < img.w; x++) {
            uint32_t &p = img.px[(size_t)y * img.w + x];
            uint32_t r = (uint32_t)(((p >> 16) & 0xFF) * keep), g = (uint32_t)(((p >> 8) & 0xFF) * keep),
                     b = (uint32_t)((p & 0xFF) * keep);
            p = (p & 0xFF000000u) | (r << 16) | (g << 8) | b;
        }
    }
}

// Pixel fonts without anti-aliasing: every glyph pixel fully on or off. VT323's outlines never
// fall on whole pixels, and its soft edges look blurry on a 15 kHz CRT.
const void *(*g_ttf_bitmap)(lv_font_glyph_dsc_t *, lv_draw_buf_t *) = nullptr;

const void *crisp_glyph_bitmap(lv_font_glyph_dsc_t *g, lv_draw_buf_t *buf)
{
    const void *r = g_ttf_bitmap(g, buf);
    if (r && g->format == LV_FONT_GLYPH_FORMAT_A8) {
        lv_draw_buf_t *d = (lv_draw_buf_t *)r;
        for (uint32_t y = 0; y < d->header.h; y++) {
            uint8_t *row = d->data + (size_t)y * d->header.stride;
            for (uint32_t x = 0; x < d->header.w; x++) row[x] = row[x] >= 96 ? 255 : 0;
        }
    }
    return r;
}

} // namespace

Ui::~Ui()
{
    destroy();
}

const lv_font_t *Ui::font(int id, int px)
{
    for (const auto &f : fonts_)
        if (f.id == id && f.px == px) return f.font;

    const unsigned char *data = id == PixelFont ? font_press_start_2p : id == MonoFont ? font_vt323 : font_inter;
    unsigned int len = id == PixelFont ? font_press_start_2p_len : id == MonoFont ? font_vt323_len : font_inter_len;
    lv_font_t *f = lv_tiny_ttf_create_data(data, len, px);
    // Press Start 2P is an 8x8 cell font: tiny_ttf rounds its baseline to 0, which draws the
    // capitals one font pixel above the line box (clipped at the top, off-centre in chips).
    if (f && id == PixelFont) f->base_line = -(px / 8);
    if (f && compact_ && id != BodyFont) {
        g_ttf_bitmap = f->get_glyph_bitmap;
        f->get_glyph_bitmap = crisp_glyph_bitmap;
    }
    fonts_.push_back({ id, px, f });
    return f;
}

int Ui::sc(float design) const
{
    return std::max(1, (int)lroundf(design * s_));
}

int Ui::pixel_font(float design) const
{
    // Press Start 2P is drawn on an 8 px grid: only sharp at multiples of 8 (PixelPerfect.cs).
    int cells = std::max(1, (int)lroundf(design * s_ / 8.0f));
    return cells * 8;
}

lv_obj_t *Ui::box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *Ui::text(lv_obj_t *parent, const lv_font_t *f, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_remove_style_all(l);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, theme::rgb(color), 0);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *Ui::chip(lv_obj_t *parent, const std::string &label, int font_px)
{
    theme::Accent a = theme::accent_for(label);
    lv_obj_t *c = box(parent);
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    fill(c, a.soft);
    border(c, a.accent, m_.chip_border);
    lv_obj_set_style_pad_hor(c, m_.chip_pad_h, 0);
    lv_obj_set_style_pad_ver(c, m_.chip_pad_v, 0);
    lv_obj_t *t = text(c, font(PixelFont, font_px), a.accent);
    set_text(t, label);
    return c;
}

void Ui::create(int w, int h, const std::string &version, bool scanlines)
{
    destroy();
    scanlines_ = scanlines;

    w_ = w;
    h_ = h;
    s_ = std::min(w / 960.0f, h / 540.0f);
    compact_ = h < 300; // 15 kHz / 240p
    // HDMI modes have square pixels. Low resolution modes go to a 4:3 CRT: 512x224 shown 4:3 has
    // pixels 0.58 as wide as tall, so a square must be ~1.7x more pixels wide than tall.
    par_ = compact_ ? (4.0f / 3.0f) * h / w : 1.0f;
    version_ = version;
    compute_metrics();

    screen_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(screen_);
    lv_obj_remove_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    fill(screen_, theme::Bg);

    bg_ = lv_image_create(screen_);
    lv_obj_set_pos(bg_, 0, 0);

    build_header();
    build_main();
    build_footer();

    lv_screen_load(screen_);

    rebuild_cover_images();
    update(model_);

    timer_ = lv_timer_create(on_timer, 33, this);
    update_clock();
}

int Ui::pw(int v) const
{
    return std::max(1, (int)lroundf(v / par_));
}

void Ui::compute_metrics()
{
    Metrics &m = m_;
    if (!compact_) {
        // MainWindow.axaml, scaled
        m.mx = sc(40), m.my = sc(30);
        m.brand_px = pixel_font(18), m.label_px = pixel_font(11), m.clock_px = pixel_font(22);
        m.title_px = pixel_font(26), m.platform_chip_px = pixel_font(12), m.chip_px = pixel_font(10);
        m.hint_px = pixel_font(10);
        m.body_font = BodyFont, m.body_px = std::max(10, sc(18)), m.body_line = sc(28);
        m.small_font = BodyFont, m.small_px = std::max(10, sc(15));
        m.mono_px = std::max(12, sc(18));
        m.header_gap = sc(12), m.stripe_top = sc(16), m.stripe_h = std::max(2, sc(4)), m.main_top = sc(24);
        m.footer_top = sc(22);
        m.led = std::max(2, sc(8));
        m.chip_pad_h = sc(8), m.chip_pad_v = sc(6), m.chip_border = std::max(1, sc(2)), m.chip_gap = sc(10);
        m.frame_border = std::max(1, sc(3)), m.frame_shadow = sc(6), m.cover_gap = sc(40), m.cover_max_w = 0.55f;
        m.row_gap = sc(14);
        m.status_pad_h = sc(10), m.status_pad_v = sc(7), m.status_gap = sc(14), m.status_shadow = sc(3);
        m.progress_h = std::max(6, sc(14)), m.progress_border = std::max(1, sc(2));
        m.banner_pad_h = sc(14), m.banner_pad_v = sc(12);
        m.title_lines = 3;
        m.caption = true, m.platform_in_tags = false, m.short_date = false, m.footer_versions = true;
    } else {
        // 15 kHz: the 8 px pixel font everywhere (sharp; VT323 looks blurry there), tight spacing. Vertical sizes
        // are lines; horizontal ones go through pw() where they should look the same.
        m.mx = pw(8), m.my = 8;
        m.brand_px = m.label_px = m.clock_px = m.title_px = m.platform_chip_px = m.chip_px = m.hint_px = 8;
        m.body_font = PixelFont, m.body_px = 8, m.body_line = 11;
        m.small_font = PixelFont, m.small_px = 8;
        m.mono_px = 16;
        m.header_gap = 4, m.stripe_top = 5, m.stripe_h = 2, m.main_top = 7, m.footer_top = 5;
        m.led = 4;
        m.chip_pad_h = pw(2), m.chip_pad_v = 1, m.chip_border = 1, m.chip_gap = 3;
        m.frame_border = 1, m.frame_shadow = 3, m.cover_gap = pw(8), m.cover_max_w = 0.37f;
        m.row_gap = 4;
        m.status_pad_h = pw(3), m.status_pad_v = 1, m.status_gap = pw(5), m.status_shadow = 2;
        m.progress_h = 6, m.progress_border = 1;
        m.banner_pad_h = pw(3), m.banner_pad_v = 1;
        m.title_lines = 2;
        m.caption = false, m.platform_in_tags = true, m.short_date = true, m.footer_versions = false;
    }
}

void Ui::destroy()
{
    if (timer_) lv_timer_delete(timer_);
    timer_ = nullptr;

    settings_ = nullptr;
    if (screen_) {
        lv_obj_t *blank = lv_obj_create(nullptr);
        lv_screen_load(blank);
        lv_obj_delete(screen_);
        screen_ = nullptr;
    }
    free_buf(bg_buf_);
    free_buf(cover_buf_);

    for (auto &f : fonts_) lv_tiny_ttf_destroy(f.font);
    fonts_.clear();
}

// ------------------------------------------------------------------ layout

void Ui::build_header()
{
    const Metrics &m = m_;
    const lv_font_t *brand_font = font(PixelFont, m.brand_px);
    const lv_font_t *label_font = font(PixelFont, m.label_px);
    const lv_font_t *clock_font = font(PixelFont, m.clock_px);

    // "RETRO GAME CARD" in three colours
    lv_obj_t *brand = box(screen_);
    lv_obj_set_pos(brand, m.mx, m.my);
    lv_obj_set_size(brand, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brand, LV_FLEX_FLOW_ROW);
    const struct {
        const char *text;
        uint32_t color;
    } words[] = { { "RETRO ", theme::Magenta }, { "GAME ", theme::Yellow }, { "CARD", theme::Cyan } };
    for (const auto &w : words) set_text(text(brand, brand_font, w.color), w.text);

    int row_y = m.my + lv_font_get_line_height(brand_font) + m.header_gap;
    int label_h = lv_font_get_line_height(label_font);

    monitor_led_ = box(screen_);
    lv_obj_set_size(monitor_led_, pw(m.led), m.led);
    lv_obj_set_pos(monitor_led_, m.mx, row_y + (label_h - m.led) / 2);

    monitor_text_ = text(screen_, label_font, theme::TextDim);
    lv_obj_set_pos(monitor_text_, m.mx + pw(m.led) + pw(compact_ ? 4 : sc(10)), row_y);

    // Clock and date, right aligned
    clock_ = text(screen_, clock_font, theme::Cyan);
    lv_obj_align(clock_, LV_ALIGN_TOP_RIGHT, -m.mx, m.my);
    date_ = text(screen_, label_font, theme::TextDim);
    lv_obj_align(date_, LV_ALIGN_TOP_RIGHT, -m.mx,
                 compact_ ? row_y : m.my + lv_font_get_line_height(clock_font) + sc(10));
}

void Ui::build_main()
{
    const Metrics &m = m_;
    const lv_font_t *brand_font = font(PixelFont, m.brand_px);
    const lv_font_t *label_font = font(PixelFont, m.label_px);
    const lv_font_t *clock_font = font(PixelFont, m.clock_px);
    const lv_font_t *hint_font = font(PixelFont, m.hint_px);
    const lv_font_t *footer_mono = font(MonoFont, m.mono_px);

    int header_h = lv_font_get_line_height(brand_font) + m.header_gap + lv_font_get_line_height(label_font);
    if (!compact_)
        header_h = std::max(header_h, lv_font_get_line_height(clock_font) + sc(10) + lv_font_get_line_height(label_font));

    // Rainbow stripe
    int stripe_y = m.my + header_h + m.stripe_top;
    int avail_w = w_ - 2 * m.mx;
    const uint32_t stripe[] = { theme::Magenta, theme::Orange, theme::Yellow, theme::Green, theme::Cyan, theme::Violet };
    for (int i = 0; i < 6; i++) {
        lv_obj_t *seg = box(screen_);
        int x0 = m.mx + avail_w * i / 6, x1 = m.mx + avail_w * (i + 1) / 6;
        lv_obj_set_pos(seg, x0, stripe_y);
        lv_obj_set_size(seg, x1 - x0, m.stripe_h);
        fill(seg, stripe[i]);
    }

    int main_y = stripe_y + m.stripe_h + m.main_top;
    int footer_h = lv_font_get_line_height(hint_font);
    if (m.footer_versions) footer_h = std::max(footer_h, lv_font_get_line_height(footer_mono));
    int main_h = h_ - m.my - footer_h - m.footer_top - main_y;

    // ----- Cover frame: square on screen, as tall as the row (SquareSplitPanel)
    int side_h = std::min(main_h - m.frame_shadow, (int)(avail_w * m.cover_max_w * par_) - m.frame_shadow);
    int side_w = pw(side_h);
    int bw = m.frame_border;
    frame_inner_w_ = side_w - 2 * pw(bw);
    frame_inner_h_ = side_h - 2 * bw;

    frame_shadow_ = box(screen_);
    lv_obj_set_pos(frame_shadow_, m.mx + pw(m.frame_shadow), main_y + m.frame_shadow);
    lv_obj_set_size(frame_shadow_, side_w, side_h);
    fill(frame_shadow_, theme::Shadow);

    frame_ = box(screen_);
    lv_obj_set_pos(frame_, m.mx, main_y);
    lv_obj_set_size(frame_, side_w, side_h);
    fill(frame_, theme::SurfaceSunken);
    border(frame_, theme::BorderStrong, bw);

    cover_img_ = lv_image_create(frame_);
    lv_obj_center(cover_img_);

    placeholder_ = box(frame_);
    lv_obj_set_size(placeholder_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(placeholder_);
    lv_obj_set_flex_flow(placeholder_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(placeholder_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(placeholder_, compact_ ? 6 : sc(18), 0);
    lv_obj_t *icon = box(placeholder_);
    int icon_h = compact_ ? 24 : std::max(16, sc(56));
    lv_obj_set_size(icon, pw(icon_h), icon_h);
    lv_obj_add_event_cb(icon, on_draw_sd_icon, LV_EVENT_DRAW_MAIN_END, nullptr);
    placeholder_text_ = text(placeholder_, label_font, theme::TextDim);

    caption_ = box(frame_);
    lv_obj_set_width(caption_, lv_pct(100));
    lv_obj_set_height(caption_, LV_SIZE_CONTENT);
    lv_obj_align(caption_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(caption_, theme::rgb(theme::SurfaceSunken), 0);
    lv_obj_set_style_bg_opa(caption_, 230, 0);
    border(caption_, theme::BorderStrong, bw);
    lv_obj_set_style_border_side(caption_, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_pad_hor(caption_, sc(14), 0);
    lv_obj_set_style_pad_ver(caption_, sc(10), 0);
    lv_obj_set_flex_flow(caption_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(caption_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(caption_, sc(12), 0);
    caption_title_ = text(caption_, label_font, theme::Text);
    lv_obj_set_flex_grow(caption_title_, 1);
    lv_label_set_long_mode(caption_title_, LV_LABEL_LONG_DOT);
    lv_obj_set_height(caption_title_, lv_font_get_line_height(label_font));
    caption_platform_ = text(caption_, label_font, theme::TextDim);

    // ----- Game details
    int dx = m.mx + side_w + pw(m.frame_shadow) + m.cover_gap;
    int dw = w_ - m.mx - dx;

    lv_obj_t *details = box(screen_);
    lv_obj_set_pos(details, dx, main_y);
    lv_obj_set_size(details, dw, main_h);
    lv_obj_set_flex_flow(details, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(details, m.row_gap, 0);

    // Platform chip (rebuilt in update())
    platform_chip_box_ = box(details);
    lv_obj_set_size(platform_chip_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    if (!compact_) lv_obj_set_style_pad_bottom(platform_chip_box_, sc(12) - m.row_gap, 0);

    const lv_font_t *title_font = font(PixelFont, m.title_px);
    title_ = text(details, title_font, theme::TextStrong);
    lv_obj_set_width(title_, lv_pct(100));
    lv_label_set_long_mode(title_, LV_LABEL_LONG_WRAP);
    int title_lh = lv_font_get_line_height(title_font);
    int title_space = compact_ ? 2 : title_lh / 2; // LineHeight = 1.5 x font size for pixel text
    lv_obj_set_style_text_line_space(title_, title_space, 0);
    lv_obj_set_style_max_height(title_, m.title_lines * title_lh + (m.title_lines - 1) * title_space, 0);

    byline_ = text(details, label_font, theme::TextMuted);
    lv_obj_set_width(byline_, lv_pct(100));
    lv_label_set_long_mode(byline_, LV_LABEL_LONG_WRAP);

    tags_ = box(details);
    lv_obj_set_size(tags_, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tags_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(tags_, m.chip_gap, 0);
    lv_obj_set_style_pad_column(tags_, pw(m.chip_gap), 0);

    // Description: takes the remaining height, scrolls with the d-pad
    // (desc_box_ takes the space, desc_view_ inside it shows whole lines only: fit_description)
    desc_box_ = box(details);
    lv_obj_set_width(desc_box_, lv_pct(100));
    lv_obj_set_flex_grow(desc_box_, 1);
    lv_obj_set_style_pad_top(desc_box_, compact_ ? 0 : sc(6), 0);
    desc_view_ = box(desc_box_);
    lv_obj_set_width(desc_view_, lv_pct(100));
    lv_obj_add_flag(desc_view_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(desc_view_, LV_SCROLLBAR_MODE_OFF);
    const lv_font_t *body = font(m.body_font, m.body_px);
    desc_ = text(desc_view_, body, theme::Text);
    lv_obj_set_width(desc_, lv_pct(100));
    lv_label_set_long_mode(desc_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(desc_, m.body_line - lv_font_get_line_height(body), 0);
    lv_obj_set_style_pad_right(desc_, compact_ ? 0 : sc(14), 0);

    // Error banner
    error_banner_ = box(details);
    lv_obj_set_size(error_banner_, lv_pct(100), LV_SIZE_CONTENT);
    fill(error_banner_, theme::RedSoft);
    border(error_banner_, theme::Red, m.chip_border);
    lv_obj_set_style_pad_hor(error_banner_, m.banner_pad_h, 0);
    lv_obj_set_style_pad_ver(error_banner_, m.banner_pad_v, 0);
    const lv_font_t *small_body = font(m.small_font, m.small_px);
    error_text_ = text(error_banner_, small_body, theme::Text);
    lv_obj_set_width(error_text_, lv_pct(100));
    lv_label_set_long_mode(error_text_, LV_LABEL_LONG_WRAP);
    if (compact_) lv_obj_set_style_text_line_space(error_text_, 3, 0);

    // Status and progress: under the cover when the cover leaves room there (15 kHz: the cover is
    // limited by the width), else at the bottom of the details column.
    lv_obj_t *status_parent = details;
    int below_y = main_y + side_h + m.frame_shadow + m.row_gap;
    int below_h = main_y + main_h - below_y;
    // stacked: [status box] / detail / progress
    int status_need = lv_font_get_line_height(label_font) + 2 * (m.status_pad_v + m.chip_border) + 2 +
                      lv_font_get_line_height(small_body) + m.progress_h + 2 * (m.row_gap + 1);
    bool stacked = compact_ && below_h >= status_need;
    if (stacked) {
        status_parent = box(screen_);
        lv_obj_set_pos(status_parent, m.mx, below_y);
        lv_obj_set_size(status_parent, side_w + pw(m.frame_shadow), below_h);
        lv_obj_set_flex_flow(status_parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(status_parent, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(status_parent, m.row_gap + 1, 0);
    }

    // Status row: [LED LABEL] detail
    lv_obj_t *status_row = box(status_parent);
    lv_obj_set_size(status_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(status_row, stacked ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_START, stacked ? LV_FLEX_ALIGN_START : LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_row, m.status_gap, 0);
    lv_obj_set_style_pad_row(status_row, 1, 0);

    lv_obj_t *status_wrap = box(status_row); // the box plus its hard shadow
    lv_obj_set_size(status_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_t *status_shadow = box(status_wrap);
    fill(status_shadow, theme::Shadow);
    status_box_ = box(status_wrap);
    lv_obj_set_size(status_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(status_box_, m.status_pad_h, 0);
    lv_obj_set_style_pad_ver(status_box_, m.status_pad_v, 0);
    lv_obj_set_flex_flow(status_box_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_box_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status_box_, pw(compact_ ? 3 : sc(9)), 0);
    status_led_ = box(status_box_);
    lv_obj_set_size(status_led_, pw(m.led), m.led);
    status_text_ = text(status_box_, label_font, theme::Yellow);
    lv_obj_set_user_data(status_box_, status_shadow);
    lv_obj_add_event_cb(status_box_, on_shadow_target_resized, LV_EVENT_SIZE_CHANGED, this);

    detail_ = text(status_row, small_body, theme::TextMuted);
    if (stacked) lv_obj_set_width(detail_, lv_pct(100));
    else lv_obj_set_flex_grow(detail_, 1);
    lv_label_set_long_mode(detail_, LV_LABEL_LONG_DOT);
    lv_obj_set_height(detail_, lv_font_get_line_height(small_body));

    // Segmented progress bar
    progress_ = box(status_parent);
    lv_obj_set_size(progress_, lv_pct(100), m.progress_h);
    fill(progress_, theme::SurfaceSunken);
    border(progress_, theme::Border, m.progress_border);
    lv_obj_add_event_cb(progress_, on_draw_progress, LV_EVENT_DRAW_MAIN_END, this);
}

void Ui::build_footer()
{
    const Metrics &m = m_;
    const lv_font_t *hint_font = font(PixelFont, m.hint_px);
    const lv_font_t *mono = font(MonoFont, m.mono_px);

    int right_w = 0;
    int footer_h = lv_font_get_line_height(hint_font);
    if (m.footer_versions) {
        lv_obj_t *right = box(screen_);
        lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_align(right, LV_ALIGN_BOTTOM_RIGHT, -m.mx, -m.my);
        lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(right, sc(8), 0);

        int led = std::max(2, sc(6));
        lv_obj_t *l1 = box(right);
        lv_obj_set_size(l1, led, led);
        fill(l1, theme::Magenta);
        set_text(text(right, mono, theme::TextDim), "LAUNCHER " + version_);
        lv_obj_t *gap = box(right);
        lv_obj_set_size(gap, sc(4), 1);
        lv_obj_t *l2 = box(right);
        lv_obj_set_size(l2, led, led);
        fill(l2, theme::Cyan);
        set_text(text(right, mono, theme::TextDim), "MISTER FPGA");

        lv_obj_update_layout(right);
        right_w = lv_obj_get_width(right) + sc(20);
        footer_h = std::max(footer_h, lv_font_get_line_height(mono));
    }

    hint_ = text(screen_, hint_font, theme::TextFaint);
    lv_obj_set_width(hint_, std::max(10, w_ - 2 * m.mx - right_w));
    lv_obj_set_height(hint_, lv_font_get_line_height(hint_font));
    lv_label_set_long_mode(hint_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(hint_, m.mx, h_ - m.my - footer_h + (footer_h - lv_font_get_line_height(hint_font)) / 2);
}

// ------------------------------------------------------------------ content

void Ui::set_cover(const Image *cover)
{
    has_cover_ = cover && !cover->empty();
    cover_src_ = has_cover_ ? *cover : Image();
    if (screen_) rebuild_cover_images();
}

void Ui::rebuild_cover_images()
{
    // Point the images at the new buffers before freeing the old ones (LVGL has no "no source")
    lv_draw_buf_t *old_bg = bg_buf_, *old_cover = cover_buf_;

    Image ambient = image_ambient(has_cover_ ? &cover_src_ : nullptr, w_, h_, scanlines_ && !compact_);
    bg_buf_ = image_to_draw_buf(ambient, true);
    if (bg_buf_) lv_image_set_src(bg_, bg_buf_);

    cover_buf_ = nullptr;
    if (has_cover_) {
        // Fit on screen (in square units), then stretch to the screen's pixel shape
        float fs = std::min(frame_inner_w_ * par_ / cover_src_.w, (float)frame_inner_h_ / cover_src_.h);
        Image fitted = image_resize(cover_src_, std::max(1, (int)lroundf(cover_src_.w * fs / par_)),
                                    std::max(1, (int)lroundf(cover_src_.h * fs)));
        if (scanlines_ && !compact_) bake_scanlines(fitted, 0.65f);
        cover_buf_ = image_to_draw_buf(fitted, false);
    }
    if (old_cover) {
        // Recreate the image object: the old buffer goes away
        lv_obj_delete(cover_img_);
        cover_img_ = lv_image_create(frame_);
        lv_obj_move_to_index(cover_img_, 0);
    }
    if (cover_buf_) {
        lv_image_set_src(cover_img_, cover_buf_);
        lv_obj_center(cover_img_);
    }

    free_buf(old_bg);
    free_buf(old_cover);
    show(cover_img_, cover_buf_ != nullptr);
    show(placeholder_, !has_cover_);
}

void Ui::update(const UiModel &model)
{
    model_ = model;
    if (!screen_) return;

    const bool has_card = model.state != UiState::Waiting;
    const theme::Accent accent = model.platform.empty() ? theme::Accent{ theme::BorderStrong, theme::Surface }
                                                        : theme::accent_for(model.platform);

    // Header
    set_text(monitor_text_, has_card ? "GAME CARD DETECTED" : "CARD MONITOR ACTIVE");
    fill(monitor_led_, has_card ? theme::Cyan : theme::Yellow);

    // Cover frame
    border(frame_, model.platform.empty() ? theme::BorderStrong : accent.accent, m_.frame_border);
    border(caption_, model.platform.empty() ? theme::BorderStrong : accent.accent, m_.frame_border);
    lv_obj_set_style_border_side(caption_, LV_BORDER_SIDE_TOP, 0);
    set_text(placeholder_text_, has_card ? "NO PREVIEW" : "INSERT CARD");
    show(caption_, has_card && m_.caption);
    set_text(caption_title_, model.title);
    set_text(caption_platform_, upper(model.platform));
    lv_obj_set_style_text_color(caption_platform_, theme::rgb(accent.accent), 0);

    // Details
    lv_obj_clean(platform_chip_box_);
    bool platform_chip = !model.platform.empty() && !m_.platform_in_tags;
    if (platform_chip) chip(platform_chip_box_, upper(model.platform), m_.platform_chip_px);
    show(platform_chip_box_, platform_chip);

    set_text(title_, model.title);
    set_text(byline_, model.byline);
    show(byline_, !model.byline.empty());

    lv_obj_clean(tags_);
    if (m_.platform_in_tags && !model.platform.empty()) chip(tags_, upper(model.platform), m_.chip_px);
    for (const auto &t : model.tags) chip(tags_, t, m_.chip_px);
    show(tags_, lv_obj_get_child_count(tags_) > 0);

    set_text(desc_, model.description);

    set_text(error_text_, model.error);
    show(error_banner_, !model.error.empty());

    set_text(detail_, model.detail);
    set_text(hint_, model.hint);

    apply_state();
    fit_description();
    lv_obj_invalidate(progress_);
}

// Sizes the description view to the whole lines that fit (no half-cut last line).
void Ui::fit_description()
{
    lv_obj_update_layout(screen_);
    const lv_font_t *f = lv_obj_get_style_text_font(desc_, 0);
    int lh = lv_font_get_line_height(f);
    int pitch = std::max(1, m_.body_line);
    int space = pitch - lh;
    int avail = lv_obj_get_content_height(desc_box_);
    int lines = std::max(0, (avail + space) / pitch);
    lv_obj_set_height(desc_view_, lines > 0 ? lines * pitch - space : 0);
    lv_obj_scroll_to_y(desc_view_, 0, LV_ANIM_OFF);
}

void Ui::apply_state()
{
    uint32_t color = theme::Yellow, soft = theme::YellowSoft;
    const char *label = "WAITING";
    switch (model_.state) {
    case UiState::Waiting: break;
    case UiState::Loading: label = "LOADING"; break;
    case UiState::Ready:
        label = "READY";
        color = theme::Cyan, soft = theme::CyanSoft;
        break;
    case UiState::Playing:
        label = "PLAYING";
        color = theme::Cyan, soft = theme::CyanSoft;
        break;
    case UiState::Error:
        label = "ERROR";
        color = theme::Red, soft = theme::RedSoft;
        break;
    }

    fill(status_box_, soft);
    border(status_box_, color, m_.chip_border);
    fill(status_led_, color);
    lv_obj_set_style_text_color(status_text_, theme::rgb(color), 0);
    set_text(status_text_, label);
}

void Ui::scroll(int direction)
{
    if (desc_view_) lv_obj_scroll_by_bounded(desc_view_, 0, -direction * m_.body_line * 2, LV_ANIM_OFF);
}

// ------------------------------------------------------------------ settings panel

void Ui::set_scanlines(bool on)
{
    if (on == scanlines_) return;
    scanlines_ = on;
    if (screen_) rebuild_cover_images();
}

void Ui::hide_settings()
{
    if (settings_) lv_obj_delete(settings_);
    settings_ = nullptr;
}

void Ui::show_settings(const std::vector<SettingsRow> &rows, int selected, const std::string &footer)
{
    hide_settings();
    if (!screen_) return;

    const lv_font_t *title_font = font(PixelFont, pixel_font(13));
    const lv_font_t *label_font = font(PixelFont, pixel_font(11));
    const lv_font_t *hint_font = font(PixelFont, pixel_font(10));

    // Dim the screen behind
    settings_ = box(screen_);
    lv_obj_set_size(settings_, w_, h_);
    lv_obj_set_style_bg_color(settings_, theme::rgb(theme::Bg), 0);
    lv_obj_set_style_bg_opa(settings_, 200, 0);

    // As wide as the longest row (label, gap, "< value >") or the footer needs, not the screen
    auto text_w = [](const lv_font_t *f, const std::string &t) {
        return (int)lv_text_get_width(t.c_str(), (uint32_t)t.size(), f, 0);
    };
    int row_pad = compact_ ? pw(8) : sc(12), panel_pad = compact_ ? pw(8) : sc(20);
    int content_w = text_w(hint_font, footer);
    for (const auto &r : rows) {
        int value_w = r.value.empty() ? 0 : text_w(label_font, "< " + r.value + " >") + pw(compact_ ? 16 : sc(32));
        content_w = std::max(content_w, text_w(label_font, r.label) + value_w + 2 * row_pad + 2 * std::max(1, sc(2)));
    }
    int panel_w = std::min(w_ - 2 * m_.mx, content_w + 2 * panel_pad + 2 * std::max(1, sc(2)));
    int sh = sc(4);

    lv_obj_t *shadow = box(settings_);
    fill(shadow, theme::Shadow);

    lv_obj_t *panel = box(settings_);
    lv_obj_set_width(panel, panel_w);
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    fill(panel, theme::Surface);
    border(panel, theme::Border, std::max(1, sc(2)));
    lv_obj_set_style_pad_hor(panel, panel_pad, 0);
    lv_obj_set_style_pad_ver(panel, compact_ ? 8 : sc(20), 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(panel, compact_ ? 2 : sc(8), 0);

    lv_obj_t *title = text(panel, title_font, theme::Yellow);
    set_text(title, "SETTINGS");
    lv_obj_set_style_pad_bottom(title, sc(10), 0);

    for (size_t i = 0; i < rows.size(); i++) {
        const bool sel = (int)i == selected;
        lv_obj_t *row = box(panel);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_hor(row, row_pad, 0);
        lv_obj_set_style_pad_ver(row, compact_ ? 3 : sc(9), 0);
        if (sel) {
            fill(row, theme::SurfaceAlt);
            border(row, theme::Cyan, std::max(1, sc(2)));
        } else {
            border(row, theme::Surface, std::max(1, sc(2)));
        }

        lv_obj_t *label = text(row, label_font, sel ? theme::TextStrong : theme::TextMuted);
        set_text(label, rows[i].label);
        if (!rows[i].value.empty()) {
            lv_obj_t *value = text(row, label_font, sel ? theme::Cyan : theme::TextDim);
            set_text(value, sel ? "< " + rows[i].value + " >" : rows[i].value);
        }
    }

    lv_obj_t *hint = text(panel, hint_font, theme::TextFaint);
    set_text(hint, footer);
    lv_obj_set_style_pad_top(hint, sc(10), 0);

    lv_obj_center(panel);
    lv_obj_update_layout(settings_);
    lv_obj_set_size(shadow, lv_obj_get_width(panel), lv_obj_get_height(panel));
    lv_obj_set_pos(shadow, lv_obj_get_x(panel) + sh, lv_obj_get_y(panel) + sh);
}

// ------------------------------------------------------------------ animation

void Ui::update_clock()
{
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);

    // No RTC and no network: the clock would show 1970. Hide it until the time is set.
    bool valid = tm.tm_year + 1900 >= 2024;
    show(clock_, valid);
    show(date_, valid);
    if (!valid) return;

    char buf[64];
    strftime(buf, sizeof(buf), "%H:%M", &tm);
    set_text(clock_, buf);
    strftime(buf, sizeof(buf), m_.short_date ? "%a %d %b" : "%A %d %B", &tm);
    set_text(date_, upper(buf));
}

void Ui::on_timer(lv_timer_t *t)
{
    Ui *ui = (Ui *)lv_timer_get_user_data(t);
    ui->tick_++;

    // LEDs blink in steps, 1.2 s period (Theme.axaml "led pulse")
    bool on = (ui->tick_ / 18) % 2 == 0;
    if (on != ui->led_on_) {
        ui->led_on_ = on;
        bool busy = ui->model_.state == UiState::Waiting || ui->model_.state == UiState::Loading;
        lv_obj_set_style_bg_opa(ui->status_led_, busy && !on ? 51 : LV_OPA_COVER, 0);
        lv_obj_set_style_bg_opa(ui->monitor_led_, ui->model_.state == UiState::Waiting && !on ? 51 : LV_OPA_COVER, 0);
    }

    if (ui->tick_ % 30 == 0) ui->update_clock();

    if (ui->model_.progress < 0) {
        ui->busy_phase_ = (ui->busy_phase_ + 1) % 75;
        lv_obj_invalidate(ui->progress_);
    }
}

void Ui::on_draw_progress(lv_event_t *e)
{
    Ui *ui = (Ui *)lv_event_get_user_data(e);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_current_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int inset = ui->m_.progress_border + (ui->compact_ ? 1 : std::max(1, ui->sc(2))); // border + padding
    a.x1 += inset, a.y1 += inset, a.x2 -= inset, a.y2 -= inset;
    int width = lv_area_get_width(&a);
    if (width <= 0) return;

    // Filled span: determinate value, or a chunk sliding across (indeterminate)
    int from = 0, to = 0;
    if (ui->model_.progress >= 0) {
        to = width * std::min(100, ui->model_.progress) / 100;
    } else {
        int chunk = width * 2 / 5;
        int x = (width + chunk) * ui->busy_phase_ / 75 - chunk;
        from = std::max(0, x);
        to = std::min(width, x + chunk);
    }

    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = theme::rgb(theme::Cyan);
    d.bg_opa = LV_OPA_COVER;

    // "Loading blocks": 7 px on, 3 px off (SegmentsCyanBrush)
    int period = ui->compact_ ? ui->pw(4) : std::max(3, ui->sc(10));
    int on = ui->compact_ ? ui->pw(3) : std::max(2, ui->sc(7));
    for (int x = (from / period) * period; x < to; x += period) {
        lv_area_t seg = { a.x1 + std::max(x, from), a.y1, a.x1 + std::min(x + on, to) - 1, a.y2 };
        if (seg.x2 >= seg.x1) lv_draw_rect(layer, &d, &seg);
    }
}

void Ui::on_draw_sd_icon(lv_event_t *e)
{
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_current_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    // Material "sd_card" icon, 24x24 grid (the box already has the screen's pixel shape)
    float ux = lv_area_get_width(&a) / 24.0f, uy = lv_area_get_height(&a) / 24.0f;
    auto X = [&](float v) { return (int32_t)lroundf(a.x1 + v * ux); };
    auto Y = [&](float v) { return (int32_t)lroundf(a.y1 + v * uy); };

    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = theme::rgb(theme::BorderStrong);
    d.bg_opa = LV_OPA_COVER;

    lv_area_t body = { X(4), Y(8), X(20) - 1, Y(22) - 1 };
    lv_area_t top = { X(10), Y(2), X(20) - 1, Y(8) - 1 };
    lv_draw_rect(layer, &d, &body);
    lv_draw_rect(layer, &d, &top);

    lv_draw_triangle_dsc_t t;
    lv_draw_triangle_dsc_init(&t);
    t.bg_color = d.bg_color;
    t.bg_opa = LV_OPA_COVER;
    t.p[0].x = X(4), t.p[0].y = Y(8);
    t.p[1].x = X(10), t.p[1].y = Y(2);
    t.p[2].x = X(10), t.p[2].y = Y(8);
    lv_draw_triangle(layer, &t);

    // Contact slots
    d.bg_color = theme::rgb(theme::SurfaceSunken);
    for (float sx : { 10.0f, 13.0f, 16.0f }) {
        lv_area_t slot = { X(sx), Y(4), X(sx + 2) - 1, Y(8) - 1 };
        lv_draw_rect(layer, &d, &slot);
    }
}

void Ui::on_shadow_target_resized(lv_event_t *e)
{
    Ui *ui = (Ui *)lv_event_get_user_data(e);
    lv_obj_t *target = (lv_obj_t *)lv_event_get_current_target(e);
    lv_obj_t *shadow = (lv_obj_t *)lv_obj_get_user_data(target);
    lv_obj_set_pos(shadow, ui->pw(ui->m_.status_shadow), ui->m_.status_shadow);
    lv_obj_set_size(shadow, lv_obj_get_width(target), lv_obj_get_height(target));
}
