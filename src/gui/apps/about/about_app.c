#include "gui/apps/about/about_app.h"
#include "gui/desktop.h"
#include "gui/font.h"
#include "gui/bmp_loader.h"
#include "pmm.h"
#include "drivers/system/sound_manager.h"

#include <stdint.h>

extern void draw_rounded_rect_buf(
    int x,
    int y,
    int w,
    int h,
    int r,
    uint32_t color
);

extern void draw_rounded_rect_alpha(
    int x,
    int y,
    int w,
    int h,
    int r,
    uint32_t color,
    uint8_t alpha
);

extern void draw_rect_buf(
    int x,
    int y,
    int w,
    int h,
    uint32_t color
);

extern void draw_pixel_buf(
    int x,
    int y,
    uint32_t color
);


/* Embedded About BMP */
extern const uint8_t about_bmp_start[];
extern const uint8_t about_bmp_end[];


/* ==========================================
   Window state
   ========================================== */

static int is_open = 0;

static int win_x = 0;
static int win_y = 0;

static int win_w = 520;
static int win_h = 320;

static int positioned = 0;

static int dragging = 0;
static int drag_ox = 0;
static int drag_oy = 0;


/* ==========================================
   Toggle
   ========================================== */

void toggle_about_app(void)
{
    is_open = !is_open;
}


/* ==========================================
   Draw About image
   ========================================== */

static void draw_about_image(
    int x,
    int y,
    int w,
    int h,
    uint32_t* buf
) {
    if (!about_bmp_start)
        return;

    static uint32_t about_buffer[180 * 180];

    draw_bmp_stretched(
        about_bmp_start,
        w,
        h,
        about_buffer
    );

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {

            int dx = x + px;
            int dy = y + py;

            if (dx < 0 || dy < 0)
                continue;

            uint32_t col = about_buffer[py * w + px];

            if ((col & 0x00FFFFFF) != 0) {
                draw_pixel_buf(
                    dx,
                    dy,
                    col
                );
            }
        }
    }

    (void)buf;
}


/* ==========================================
   Render window
   ========================================== */

void render_about_app_window(
    uint32_t* buf,
    int scr_w,
    int scr_h,
    int mx,
    int my,
    int btn,
    int click
) {
    if (!is_open)
        return;


    /* Center window once */

    if (!positioned) {

        win_x = (scr_w - win_w) / 2;
        win_y = (scr_h - win_h) / 2;

        positioned = 1;
    }


    /* ======================================
       Dragging
       ====================================== */

    int traffic_zone_w = 90;

    if (
        btn &&
        !dragging &&
        win_drag_available() &&
        mx >= win_x &&
        mx <= win_x + win_w - traffic_zone_w &&
        my >= win_y &&
        my <= win_y + 36
    ) {
        dragging = 1;

        drag_ox = mx - win_x;
        drag_oy = my - win_y;

        win_drag_claim();
    }


    if (!btn)
        dragging = 0;


    if (dragging) {

        win_x = mx - drag_ox;
        win_y = my - drag_oy;
    }

    /* BAG: клик по кнопке закрытия реагировал, даже когда курсор
     * физически над другим, визуально более верхним окном -- клик
     * "проваливался" сквозь чужой title bar. */
    win_report_rect(WIN_ID_ABOUT_, win_x, win_y, win_w, win_h, is_open);
    int occluded = win_click_occluded(WIN_ID_ABOUT_, mx, my);


    /* ======================================
       Traffic lights
       ====================================== */

    int tl_size = 14;
    int tl_gap = 10;
    int tl_right_margin = 14;

    int tl_y =
        win_y +
        (36 - tl_size) / 2;

    int close_x =
        win_x +
        win_w -
        tl_right_margin -
        tl_size;

    int minimize_x =
        close_x -
        tl_gap -
        tl_size;

    int zoom_x =
        minimize_x -
        tl_gap -
        tl_size;


    /* Расширенный хитбокс (+8px в каждую сторону) */
    int hit_padding = 8;

    int hover_close =
        mx >= (close_x - hit_padding) &&
        mx <= (close_x + tl_size + hit_padding) &&
        my >= (tl_y - hit_padding) &&
        my <= (tl_y + tl_size + hit_padding);


    /* ======================================
       Window shadow
       ====================================== */

    /* Fast, clean 2-pass soft drop shadow */
    draw_rounded_rect_alpha(win_x - 6, win_y - 2 + 8, win_w + 12, win_h + 10, 18, 0x00000000, 24);
    draw_rounded_rect_alpha(win_x - 2, win_y + 3,     win_w + 4,  win_h + 4,  15, 0x00000000, 42);


    /* ======================================
       Main window
       ====================================== */

    draw_rounded_rect_buf(
        win_x,
        win_y,
        win_w,
        win_h,
        12,
        0x00F6F6F8
    );


    /* ======================================
       Header
       ====================================== */

    draw_rounded_rect_buf(
        win_x,
        win_y,
        win_w,
        36,
        12,
        0x00ECECEF
    );

    draw_rect_buf(
        win_x,
        win_y + 18,
        win_w,
        18,
        0x00ECECEF
    );

    draw_rect_buf(
        win_x,
        win_y + 35,
        win_w,
        1,
        0x00D8D8DC
    );


    /* ======================================
       Traffic lights
       ====================================== */

    draw_rounded_rect_buf(
        zoom_x,
        tl_y,
        tl_size,
        tl_size,
        4,
        0x0028C840
    );

    draw_rounded_rect_buf(
        minimize_x,
        tl_y,
        tl_size,
        tl_size,
        4,
        0x00FFBD2E
    );

    draw_rounded_rect_buf(
        close_x,
        tl_y,
        tl_size,
        tl_size,
        4,
        hover_close
            ? 0x00FF6259
            : 0x00FF5F57
    );


    /* Close */

    if (click && hover_close && !occluded) {

        is_open = 0;
        dragging = 0;

        return;
    }


    /* ======================================
       Content
       ====================================== */

    int content_top = win_y + 36;

    int content_h =
        win_h - 36;


    int image_size = 150;

    int image_x =
        win_x + 45;

    int image_y =
        content_top +
        (content_h - image_size) / 2;


    static uint32_t image_buffer[150 * 150];

    draw_bmp_stretched(
        about_bmp_start,
        image_size,
        image_size,
        image_buffer
    );


    for (int py = 0; py < image_size; py++) {

        for (int px = 0; px < image_size; px++) {

            int dx =
                image_x + px;

            int dy =
                image_y + py;

            uint32_t col = image_buffer[
                py * image_size + px
            ];

            if ((col & 0x00FFFFFF) != 0) {
                draw_pixel_buf(
                    dx,
                    dy,
                    col
                );
            }
        }
    }


    /* ======================================
       Text / Specifications
       ====================================== */

    int text_x = image_x + image_size + 32;
    int text_y = content_top + 36;

    draw_string("ByteOS Nord", text_x, text_y, 0x001C1C1E, buf, scr_w);

    text_y += 24;
    draw_string("Version 0.5.2 (x86_64)", text_x, text_y, 0x00007AFF, buf, scr_w);

    text_y += 22;
    uint64_t ram_mb = pmm_get_total_memory() / (1024 * 1024);
    char ram_str[32] = "Memory: ";
    int ri = 8;
    if (ram_mb == 0) ram_str[ri++] = '0';
    else {
        char rev[12]; int rk = 0; uint64_t rv = ram_mb;
        while (rv > 0) { rev[rk++] = '0' + (rv % 10); rv /= 10; }
        while (rk > 0) ram_str[ri++] = rev[--rk];
    }
    ram_str[ri++] = ' '; ram_str[ri++] = 'M'; ram_str[ri++] = 'B'; ram_str[ri] = 0;
    draw_string(ram_str, text_x, text_y, 0x003A3A3C, buf, scr_w);

    text_y += 20;
    char snd_str[48] = "Audio: ";
    const char *dev_name = sound_get_device_name();
    int si = 7;
    for (int k = 0; dev_name[k] && si < 47; k++) snd_str[si++] = dev_name[k];
    snd_str[si] = 0;
    draw_string(snd_str, text_x, text_y, 0x003A3A3C, buf, scr_w);

    text_y += 20;
    draw_string("Kernel: Limine Bare-Metal", text_x, text_y, 0x008E8E93, buf, scr_w);

    text_y += 20;
    draw_string("Developer: Igoreeek228", text_x, text_y, 0x008E8E93, buf, scr_w);


    /* ======================================
       Footer
       ====================================== */

    const char* footer =
        "2026. GNU General Public License v3.0.";


    int footer_w =
        font_text_width(footer);


    draw_string(

        footer,

        win_x +
        (win_w - footer_w) / 2,

        win_y +
        win_h -
        26,

        0x00B0B0B5,

        buf,

        scr_w
    );
}