/*
 * terminal_app.c
 *
 * Приложение "Terminal" для IgorOS Nord.
 * Продвинутая консоль с поддержкой neofetch, статистики памяти, таймера и управления питанием.
 */

#include "terminal_app.h"
#include "gui/desktop.h"
#include "gui/font.h"
#include "gui/anim/genie_anim.h"
#include "gui/anim/win_chrome.h"
#include "pmm.h"
#include "kernel/timer.h"
#include "drivers/system/sound_manager.h"

#include <stdint.h>

extern void draw_rounded_rect_buf(int x, int y, int w, int h, int r, uint32_t color);
extern void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
extern void draw_rect_buf(int x, int y, int w, int h, uint32_t color);

#define TERM_ROWS      15
#define TERM_LINE_MAX  64
#define TERM_INPUT_MAX 48
#define TERM_DOCK_INDEX 1

static int is_open = 0;
static int minimized = 0;
static int win_x = 0, win_y = 0;
static int win_w = 600, win_h = 390;
static int positioned = 0;
static int dragging = 0;
static int drag_ox = 0, drag_oy = 0;
static genie_state_t genie;

static char lines[TERM_ROWS][TERM_LINE_MAX];
static int line_count = 0;
static char input[TERM_INPUT_MAX];
static uint32_t input_len = 0;
static int booted = 0;

static inline void outb_local(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb_local(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw_local(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static uint32_t t_strlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

static void t_strcpy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int t_streq(const char *a, const char *b) {
    uint32_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void t_split_first_word(const char *line, char *cmd, uint32_t cmd_max,
                                char *rest, uint32_t rest_max) {
    uint32_t i = 0, ci = 0;
    while (line[i] == ' ') i++;
    while (line[i] && line[i] != ' ' && ci < cmd_max - 1) cmd[ci++] = line[i++];
    cmd[ci] = 0;
    while (line[i] == ' ') i++;
    uint32_t ri = 0;
    while (line[i] && ri < rest_max - 1) rest[ri++] = line[i++];
    rest[ri] = 0;
}

static void term_push_line(const char *line) {
    if (line_count < TERM_ROWS) {
        t_strcpy(lines[line_count], line, TERM_LINE_MAX);
        line_count++;
    } else {
        for (int i = 1; i < TERM_ROWS; i++) t_strcpy(lines[i - 1], lines[i], TERM_LINE_MAX);
        t_strcpy(lines[TERM_ROWS - 1], line, TERM_LINE_MAX);
    }
}

static uint8_t bcd_to_bin(uint8_t val) {
    return ((val >> 4) * 10) + (val & 0x0F);
}

static void term_exec(const char *cmdline) {
    char prompt_line[TERM_LINE_MAX];
    t_strcpy(prompt_line, "~ $ ", TERM_LINE_MAX);
    uint32_t base = t_strlen(prompt_line);
    uint32_t i = 0;
    while (cmdline[i] && base + i < TERM_LINE_MAX - 1) { prompt_line[base + i] = cmdline[i]; i++; }
    prompt_line[base + i] = 0;
    term_push_line(prompt_line);

    char cmd[16], rest[TERM_INPUT_MAX];
    t_split_first_word(cmdline, cmd, sizeof(cmd), rest, sizeof(rest));

    if (t_strlen(cmd) == 0) {
        return;
    } else if (t_streq(cmd, "help")) {
        term_push_line("COMMANDS:");
        term_push_line("  FETCH     - SYSTEM SPECS AND LOGO");
        term_push_line("  MEM       - PHYSICAL RAM USAGE");
        term_push_line("  UPTIME    - SYSTEM RUNTIME");
        term_push_line("  DATE      - HARDWARE RTC CLOCK");
        term_push_line("  WHOAMI    - CURRENT LOGGED USER");
        term_push_line("  ECHO      - PRINT GIVEN STRING");
        term_push_line("  CLEAR     - WIPE TERMINAL SCREEN");
        term_push_line("  REBOOT    - RESTART MACHINE");
        term_push_line("  SHUTDOWN  - ACPI POWER OFF");
        term_push_line("  PANIC     - TRIGGER BSOD DEMO");
    } else if (t_streq(cmd, "clear")) {
        line_count = 0;
    } else if (t_streq(cmd, "pwd")) {
        term_push_line("/");
    } else if (t_streq(cmd, "whoami")) {
        term_push_line("root@byteos");
    } else if (t_streq(cmd, "echo")) {
        term_push_line(rest);
    } else if (t_streq(cmd, "uptime")) {
        uint64_t ms = timer_millis();
        uint32_t sec = (uint32_t)(ms / 1000);
        uint32_t min = sec / 60;
        sec %= 60;
        char buf[TERM_LINE_MAX];
        char num[16];
        t_strcpy(buf, "UPTIME: ", TERM_LINE_MAX);
        int ni = 0;
        if (min == 0) num[ni++] = '0';
        else {
            uint32_t m = min;
            char rev[12]; int ri = 0;
            while (m > 0) { rev[ri++] = '0' + (m % 10); m /= 10; }
            while (ri > 0) num[ni++] = rev[--ri];
        }
        num[ni++] = 'm'; num[ni++] = ' ';
        if (sec < 10) num[ni++] = '0';
        num[ni++] = '0' + (sec / 10);
        num[ni++] = '0' + (sec % 10);
        num[ni++] = 's';
        num[ni] = 0;
        uint32_t b2 = t_strlen(buf);
        for (int k = 0; num[k]; k++) buf[b2 + k] = num[k];
        buf[b2 + ni] = 0;
        term_push_line(buf);
    } else if (t_streq(cmd, "mem") || t_streq(cmd, "free")) {
        uint64_t total = pmm_get_total_memory() / (1024 * 1024);
        uint64_t used = pmm_get_used_memory() / (1024 * 1024);
        uint64_t free_mem = pmm_get_free_memory() / (1024 * 1024);

        char l1[TERM_LINE_MAX], l2[TERM_LINE_MAX];
        t_strcpy(l1, "RAM: TOTAL ", TERM_LINE_MAX);
        /* format MB */
        char mb[32];
        int mi = 0;
        uint64_t v = total;
        char rev[16]; int ri = 0;
        if (v == 0) rev[ri++] = '0';
        while (v > 0) { rev[ri++] = '0' + (v % 10); v /= 10; }
        while (ri > 0) mb[mi++] = rev[--ri];
        mb[mi++] = ' '; mb[mi++] = 'M'; mb[mi++] = 'B'; mb[mi] = 0;
        uint32_t b2 = t_strlen(l1);
        for (int k = 0; mb[k]; k++) l1[b2 + k] = mb[k];
        l1[b2 + mi] = 0;
        term_push_line(l1);

        t_strcpy(l2, "     USED ", TERM_LINE_MAX);
        mi = 0; v = used; ri = 0;
        if (v == 0) rev[ri++] = '0';
        while (v > 0) { rev[ri++] = '0' + (v % 10); v /= 10; }
        while (ri > 0) mb[mi++] = rev[--ri];
        mb[mi++] = ' '; mb[mi++] = 'M'; mb[mi++] = 'B';
        mb[mi++] = ' '; mb[mi++] = '|'; mb[mi++] = ' ';
        mb[mi++] = 'F'; mb[mi++] = 'R'; mb[mi++] = 'E'; mb[mi++] = 'E'; mb[mi++] = ' ';
        v = free_mem; ri = 0;
        if (v == 0) rev[ri++] = '0';
        while (v > 0) { rev[ri++] = '0' + (v % 10); v /= 10; }
        while (ri > 0) mb[mi++] = rev[--ri];
        mb[mi++] = ' '; mb[mi++] = 'M'; mb[mi++] = 'B'; mb[mi] = 0;
        b2 = t_strlen(l2);
        for (int k = 0; mb[k]; k++) l2[b2 + k] = mb[k];
        l2[b2 + mi] = 0;
        term_push_line(l2);
    } else if (t_streq(cmd, "fetch") || t_streq(cmd, "neofetch")) {
        term_push_line("  _        ___  ____    OS: ByteOS 0.5.2 (x86_64)");
        term_push_line(" (_)___   / _ \\/ ___|   KERNEL: Limine Microkernel");
        term_push_line(" | / _ \\ | | | \\___ \\   HOST: PC Compatible (x86_64)");

        uint64_t total = pmm_get_total_memory() / (1024 * 1024);
        uint64_t used = pmm_get_used_memory() / (1024 * 1024);
        const char *snd = sound_get_device_name();

        char aud_line[TERM_LINE_MAX];
        t_strcpy(aud_line, " | \\___/ | |_| |___) |  AUDIO: ", TERM_LINE_MAX);
        uint32_t b2 = t_strlen(aud_line);
        for (int k = 0; snd[k] && b2 + k < TERM_LINE_MAX - 1; k++) aud_line[b2 + k] = snd[k];
        aud_line[b2 + t_strlen(snd)] = 0;
        term_push_line(aud_line);

        char mem_line[TERM_LINE_MAX];
        t_strcpy(mem_line, " |_|      \\___/|____/   RAM: ", TERM_LINE_MAX);
        char ram_str[32];
        int ri = 0;
        uint64_t v = used;
        char rev[16]; int rpos = 0;
        if (v == 0) rev[rpos++] = '0';
        while (v > 0) { rev[rpos++] = '0' + (v % 10); v /= 10; }
        while (rpos > 0) ram_str[ri++] = rev[--rpos];
        ram_str[ri++] = ' '; ram_str[ri++] = '/'; ram_str[ri++] = ' ';
        v = total; rpos = 0;
        if (v == 0) rev[rpos++] = '0';
        while (v > 0) { rev[rpos++] = '0' + (v % 10); v /= 10; }
        while (rpos > 0) ram_str[ri++] = rev[--rpos];
        ram_str[ri++] = ' '; ram_str[ri++] = 'M'; ram_str[ri++] = 'B'; ram_str[ri] = 0;
        uint32_t mb_idx = t_strlen(mem_line);
        for (int k = 0; ram_str[k]; k++) mem_line[mb_idx + k] = ram_str[k];
        mem_line[mb_idx + ri] = 0;
        term_push_line(mem_line);
    } else if (t_streq(cmd, "date")) {
        outb_local(0x70, 0x00); uint8_t s = inb_local(0x71);
        outb_local(0x70, 0x02); uint8_t m = inb_local(0x71);
        outb_local(0x70, 0x04); uint8_t h = inb_local(0x71);
        outb_local(0x70, 0x07); uint8_t day = inb_local(0x71);
        outb_local(0x70, 0x08); uint8_t mon = inb_local(0x71);
        outb_local(0x70, 0x09); uint8_t yr = inb_local(0x71);
        outb_local(0x70, 0x0B); uint8_t regB = inb_local(0x71);

        if (!(regB & 0x04)) {
            s = bcd_to_bin(s); m = bcd_to_bin(m); h = bcd_to_bin(h);
            day = bcd_to_bin(day); mon = bcd_to_bin(mon); yr = bcd_to_bin(yr);
        }

        char date_str[TERM_LINE_MAX];
        t_strcpy(date_str, "DATE: 20", TERM_LINE_MAX);
        int p = t_strlen(date_str);
        date_str[p++] = '0' + (yr / 10);
        date_str[p++] = '0' + (yr % 10);
        date_str[p++] = '-';
        date_str[p++] = '0' + (mon / 10);
        date_str[p++] = '0' + (mon % 10);
        date_str[p++] = '-';
        date_str[p++] = '0' + (day / 10);
        date_str[p++] = '0' + (day % 10);
        date_str[p++] = ' ';
        date_str[p++] = '0' + (h / 10);
        date_str[p++] = '0' + (h % 10);
        date_str[p++] = ':';
        date_str[p++] = '0' + (m / 10);
        date_str[p++] = '0' + (m % 10);
        date_str[p++] = ':';
        date_str[p++] = '0' + (s / 10);
        date_str[p++] = '0' + (s % 10);
        date_str[p] = 0;
        term_push_line(date_str);
    } else if (t_streq(cmd, "reboot")) {
        term_push_line("REBOOTING SYSTEM...");
        /* 8042 Keyboard controller pulse reset line */
        uint8_t good = 0x02;
        while (good & 0x02) good = inb_local(0x64);
        outb_local(0x64, 0xFE);
        /* Fallback: triple fault */
        __asm__ volatile ("lidt (%%rax)" : : "a"(0));
        __asm__ volatile ("int $3");
    } else if (t_streq(cmd, "shutdown") || t_streq(cmd, "poweroff")) {
        term_push_line("SHUTTING DOWN SYSTEM...");
        outw_local(0x604, 0x2000);  /* QEMU shutdown */
        outw_local(0xB004, 0x2000); /* Bochs shutdown */
        outw_local(0x4004, 0x3400); /* Virtualbox shutdown */
    } else if (t_streq(cmd, "panic") || t_streq(cmd, "crash")) {
        __asm__ volatile (
            "xor %%eax, %%eax\n\t"
            "xor %%edx, %%edx\n\t"
            "xor %%ecx, %%ecx\n\t"
            "idiv %%ecx\n\t"
            : : : "eax", "edx", "ecx"
        );
    } else {
        char line[TERM_LINE_MAX];
        t_strcpy(line, "UNKNOWN: ", TERM_LINE_MAX);
        uint32_t b2 = t_strlen(line);
        uint32_t j = 0;
        while (cmd[j] && b2 + j < TERM_LINE_MAX - 1) { line[b2 + j] = cmd[j]; j++; }
        line[b2 + j] = 0;
        term_push_line(line);
    }
}

void toggle_terminal_app(void)
{
    int dx, dy;
    genie_dock_icon_point(TERM_DOCK_INDEX, &dx, &dy);

    if (genie_is_animating(&genie))
        genie_cancel(&genie);

    if (is_open && minimized) {
        genie_start_open(&genie, dx, dy);
        minimized = 0;
        dragging = 0;
        return;
    }

    if (is_open) {
        genie_start_close(&genie, dx, dy);
        is_open = 0;
        minimized = 0;
        dragging = 0;
        return;
    }

    is_open = 1;
    minimized = 0;
    dragging = 0;
    if (!booted) {
        term_push_line("ByteOS Terminal v0.5.2");
        term_push_line("Type HELP for available commands.");
        booted = 1;
    }
    genie_start_open(&genie, dx, dy);
}

void terminal_app_feed_key(char key)
{
    if (!is_open) return;

    if (key == '\r' || key == '\n') {
        input[input_len] = 0;
        term_exec(input);
        input_len = 0;
        input[0] = 0;
    } else if (key == 8 /* backspace */) {
        if (input_len > 0) {
            input_len--;
            input[input_len] = 0;
        }
    } else if (key >= 32 && key <= 126) {
        if (input_len < TERM_INPUT_MAX - 1) {
            input[input_len++] = key;
            input[input_len] = 0;
        }
    }
}

int terminal_app_is_open(void) {
    return is_open && !minimized;
}

void render_terminal_app_window(
    uint32_t* buf,
    int scr_w,
    int scr_h,
    int mx,
    int my,
    int click,
    int single_click
)
{
    int btn = click;
    if (!is_open) return;

    int mid_genie = genie_is_animating(&genie);
    if (!positioned) {
        win_x = (scr_w - win_w) / 2;
        win_y = (scr_h - win_h) / 2;
        positioned = 1;
    }

    int header_h = 36;
    int occluded = win_click_occluded(WIN_ID_TERMINAL_, mx, my);

    if (single_click && !occluded && !mid_genie) {
        if (mx >= win_x && mx <= win_x + win_w && my >= win_y && my <= win_y + win_h) {
            win_drag_claim();
        }
    }

    if (btn && !dragging && win_drag_available() && !occluded && !mid_genie) {
        if (mx >= win_x && mx <= (win_x + win_w - 90) && my >= win_y && my <= (win_y + header_h)) {
            win_drag_claim();
            dragging = 1;
            drag_ox = mx - win_x;
            drag_oy = my - win_y;
        }
    }

    if (!btn) {
        dragging = 0;
    }

    if (dragging) {
        win_x = mx - drag_ox;
        win_y = my - drag_oy;
        if (win_y < 25) win_y = 25;
    }

    win_report_rect(WIN_ID_TERMINAL_, win_x, win_y, win_w, win_h, 1);

    int draw_x, draw_y, draw_w, draw_h;
    genie_get_rect(&genie, win_x, win_y, win_w, win_h, &draw_x, &draw_y, &draw_w, &draw_h);

    /* Fast, clean 2-pass soft drop shadow */
    draw_rounded_rect_alpha(draw_x - 6, draw_y - 2 + 8, draw_w + 12, draw_h + 10, 22, 0x00000000, 24);
    draw_rounded_rect_alpha(draw_x - 2, draw_y + 3,     draw_w + 4,  draw_h + 4,  19, 0x00000000, 42);

    /* Modern Dark Acrylic Terminal Body */
    draw_rounded_rect_buf(draw_x, draw_y, draw_w, draw_h, 18, 0x00141416);

    if (mid_genie) return;

    /* Frosted header */
    draw_rounded_rect_alpha(win_x, win_y, win_w, header_h, 18, 0x00FFFFFF, 12);
    draw_rect_buf(win_x, win_y + header_h / 2, win_w, header_h / 2, 0x00141416);

    /* Subtle header bottom divider */
    draw_rect_buf(win_x, win_y + header_h - 1, win_w, 1, 0x002C2C2E);

    /* Window title centered */
    const char *title = "Terminal - root@byteos";
    int tw = font_text_width(title);
    draw_string(title, win_x + (win_w - tw) / 2, win_y + (header_h - 14) / 2, 0x00A1A1A6, buf, (uint32_t)scr_w);

    /* Traffic Lights */
    int min_click = 0, zoom_click = 0;
    if (win_chrome_traffic_lights(
        WIN_ID_TERMINAL_, win_x, win_y, win_w, header_h,
        mx, my, single_click, occluded,
        &min_click, &zoom_click
    )) {
        int dx, dy;
        genie_dock_icon_point(TERM_DOCK_INDEX, &dx, &dy);
        genie_start_close(&genie, dx, dy);
        is_open = 0;
        dragging = 0;
        return;
    }

    if (min_click) {
        int dx, dy;
        genie_dock_icon_point(TERM_DOCK_INDEX, &dx, &dy);
        genie_start_minimize(&genie, dx, dy);
        minimized = 1;
        dragging = 0;
        return;
    }

    if (zoom_click) {
        static int pre_x, pre_y, pre_w, pre_h, is_zoomed = 0;
        if (!is_zoomed) {
            pre_x = win_x; pre_y = win_y; pre_w = win_w; pre_h = win_h;
            win_x = 40; win_y = 36; win_w = scr_w - 80; win_h = scr_h - 76;
            is_zoomed = 1;
        } else {
            win_x = pre_x; win_y = pre_y; win_w = pre_w; win_h = pre_h;
            is_zoomed = 0;
        }
        return;
    }

    /* Content rendering */
    int content_x = win_x + 16;
    int content_y = win_y + header_h + 10;
    int line_h = 18;

    for (int i = 0; i < line_count; i++) {
        uint32_t color = 0x00F5F5F7; /* Clean off-white font */
        if (lines[i][0] == '~' && lines[i][2] == '$') {
            color = 0x000A84FF; /* Accent blue prompt */
        } else if (lines[i][0] == 'U' && lines[i][1] == 'N') {
            color = 0x00FF453A; /* Red error */
        } else if (lines[i][0] == ' ' && lines[i][1] == ' ' && lines[i][2] == '_') {
            color = 0x0030D158; /* Green ASCII logo */
        } else if (lines[i][0] == 'C' && lines[i][1] == 'O' && lines[i][2] == 'M') {
            color = 0x00FFD60A; /* Yellow header */
        }
        draw_string(lines[i], content_x, content_y + i * line_h, color, buf, (uint32_t)scr_w);
    }

    /* Interactive prompt line */
    char prompt[TERM_LINE_MAX + TERM_INPUT_MAX];
    t_strcpy(prompt, "~ $ ", sizeof(prompt));
    uint32_t base = t_strlen(prompt);
    uint32_t k = 0;
    while (input[k] && base + k < sizeof(prompt) - 1) { prompt[base + k] = input[k]; k++; }
    prompt[base + k] = 0;

    int prompt_y = content_y + line_count * line_h;
    draw_string(prompt, content_x, prompt_y, 0x000A84FF, buf, (uint32_t)scr_w);

    int caret_x = content_x + font_text_width(prompt) + 2;
    /* Soft blinking cursor indicator */
    uint64_t tick = timer_millis() / 500;
    if ((tick % 2) == 0) {
        draw_rect_buf(caret_x, prompt_y + 1, 7, 14, 0x000A84FF);
    }
}
