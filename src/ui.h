// The main screen, laid out like the PC launcher's MainWindow.axaml (designed at 960x540 and
// scaled to the framebuffer).
#pragma once

#include <string>
#include <vector>

#include "image.h"
#include "lvgl.h"

enum class UiState {
    Waiting, // no card
    Ready,   // card shown, waiting for A (auto start cancelled, or game ended)
    Loading, // auto start countdown / sending the core
    Playing, // the game core is loaded
    Error,
};

struct UiModel {
    UiState state = UiState::Waiting;
    std::string title;
    std::string description;
    std::string platform;
    std::string byline;
    std::vector<std::string> tags;
    std::string detail; // next to the status box
    std::string error;  // red banner ("" hides it)
    std::string hint;   // footer, left
    int progress = -1;  // 0..100, or -1 for the indeterminate "busy" animation
};

struct SettingsRow {
    std::string label;
    std::string value; // "" for an action row
};

class Ui {
public:
    ~Ui();

    // Builds the screen for a w x h display (the active LVGL display).
    void create(int w, int h, const std::string &version, bool scanlines);
    void destroy();

    // Cover art of the card (nullptr: none). Resampled once to the frame and the background.
    void set_cover(const Image *cover);

    void update(const UiModel &model);

    // Scrolls the description (d-pad up / down).
    void scroll(int direction);

    // Settings panel over the screen (rebuilt at each call), and its removal.
    void show_settings(const std::vector<SettingsRow> &rows, int selected, const std::string &footer);
    void hide_settings();

    void set_scanlines(bool on);

private:

    const lv_font_t *font(int id, int px);
    int sc(float design) const;            // design units -> pixels
    int pixel_font(float design) const;    // Press Start 2P size, snapped to its 8 px grid
    lv_obj_t *box(lv_obj_t *parent);       // style-less container
    lv_obj_t *text(lv_obj_t *parent, const lv_font_t *f, uint32_t color);
    lv_obj_t *chip(lv_obj_t *parent, const std::string &label, int font_px);
    void build_header();
    void build_main();
    void build_footer();
    void apply_state();
    void update_clock();
    void rebuild_cover_images();

    static void on_draw_progress(lv_event_t *e);
    static void on_draw_sd_icon(lv_event_t *e);
    static void on_shadow_target_resized(lv_event_t *e);
    static void on_timer(lv_timer_t *t);

    // Every size of the layout, in pixels: the 960x540 design scaled (HDMI), or hand-tuned for
    // 15 kHz / 240p where a few hundred pixels must stay readable.
    struct Metrics {
        int mx, my;                             // screen margins
        int brand_px, label_px, clock_px, title_px, platform_chip_px, chip_px, hint_px; // pixel font
        int body_font, body_px, body_line;      // description
        int small_font, small_px;               // status detail, error
        int mono_px;                            // footer versions
        int header_gap, stripe_top, stripe_h, main_top, footer_top;
        int led;
        int chip_pad_h, chip_pad_v, chip_border, chip_gap;
        int frame_border, frame_shadow, cover_gap;
        float cover_max_w;                      // fraction of the width the cover may take
        int row_gap;
        int status_pad_h, status_pad_v, status_gap, status_shadow;
        int progress_h, progress_border;
        int banner_pad_h, banner_pad_v;
        int title_lines;
        bool caption, platform_in_tags, short_date, footer_versions;
    };
    void compute_metrics();
    int pw(int v) const;                        // width in pixels of something v pixels tall

    int w_ = 0, h_ = 0;
    float s_ = 1;
    float par_ = 1; // pixel aspect ratio (width / height of one pixel on the screen)
    Metrics m_{};
    bool compact_ = false;
    bool scanlines_ = true;
    lv_obj_t *settings_ = nullptr;
    std::string version_;
    UiModel model_;
    Image cover_src_;
    bool has_cover_ = false;

    struct FontEntry {
        int id, px;
        lv_font_t *font;
    };
    std::vector<FontEntry> fonts_;

    lv_obj_t *screen_ = nullptr;
    lv_obj_t *bg_ = nullptr;
    lv_draw_buf_t *bg_buf_ = nullptr;
    lv_draw_buf_t *cover_buf_ = nullptr;

    // header
    lv_obj_t *monitor_led_ = nullptr, *monitor_text_ = nullptr;
    lv_obj_t *clock_ = nullptr, *date_ = nullptr;

    // cover frame
    lv_obj_t *frame_ = nullptr, *frame_shadow_ = nullptr, *cover_img_ = nullptr;
    lv_obj_t *placeholder_ = nullptr, *placeholder_text_ = nullptr;
    lv_obj_t *caption_ = nullptr, *caption_title_ = nullptr, *caption_platform_ = nullptr;
    int frame_inner_w_ = 0, frame_inner_h_ = 0;

    // details
    lv_obj_t *platform_chip_box_ = nullptr;
    lv_obj_t *title_ = nullptr, *byline_ = nullptr, *tags_ = nullptr;
    lv_obj_t *desc_box_ = nullptr, *desc_view_ = nullptr, *desc_ = nullptr;
    void fit_description();
    lv_obj_t *error_banner_ = nullptr, *error_text_ = nullptr;
    lv_obj_t *status_box_ = nullptr, *status_led_ = nullptr, *status_text_ = nullptr, *detail_ = nullptr;
    lv_obj_t *progress_ = nullptr;

    // footer
    lv_obj_t *hint_ = nullptr;

    lv_timer_t *timer_ = nullptr;
    uint32_t tick_ = 0;
    bool led_on_ = true;
    int busy_phase_ = 0;
};
