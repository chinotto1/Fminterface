/* SPDX-License-Identifier: GPL-3.0-only
 * Sequencer page for the audio interface. SEL opens it from Home. HOME leaves.
 * Hold REC captures; a stopped empty pattern uses Melodee nightly 3fe570d's
 * free take, including BPM detection. Hold EDIT undoes that capture. */
#include "music.h"
static int8_t mus_ui_oct;
static uint32_t mus_ui_rec_ms, mus_ui_edit_ms, mus_ui_seen_step;
static uint8_t mus_ui_rec, mus_ui_rec_done, mus_ui_edit, mus_ui_edit_done, mus_ui_seen_play;
static char mus_ui_seen_msg[40];
static unsigned mus_ui_cat(char *d, unsigned n, const char *s)
{
    while (*s && n + 1u < 40u) d[n++] = *s++;
    d[n] = 0;
    return n;
}
static unsigned mus_ui_num(char *d, unsigned n, int v)
{
    char tmp[8];
    unsigned k = 0;
    uint32_t u;
    if (v < 0 && n + 1u < 40u) { d[n++] = '-'; v = -v; }
    u = (uint32_t)v;
    do { tmp[k++] = (char)('0' + u % 10u); u /= 10u; } while (u && k < 8u);
    while (k && n + 1u < 40u) d[n++] = tmp[--k];
    d[n] = 0;
    return n;
}
static void mus_ui_draw(void)
{
    static const char *const mode[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD", "REPEAT"};
    static const char *const divs[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
    char line[40], dots[17];
    unsigned n = 0, i;
    uint32_t len = mus.len[mus.sel] ? mus.len[mus.sel] : 1u;
    uint32_t head = mus.playing ? mus_playhead() : 0;
    uint32_t base = len > 16u ? head / 16u * 16u : 0;
    uint32_t div = mus.div[mus.sel] < 10u ? mus.div[mus.sel] : 2u;
    n = mus_ui_cat(line, 0, "TRACK ");
    n = mus_ui_num(line, n, (int)mus.sel + 1);
    n = mus_ui_cat(line, n, "   ");
    n = mus_ui_num(line, n, (int)mus.bpm);
    mus_ui_cat(line, n, " BPM");
    draw_text_box(0, 24, 240, &FONT_T, line, C_WHITE, 0);
    n = mus_ui_cat(line, 0, divs[div]);
    n = mus_ui_cat(line, n, "  LEN ");
    n = mus_ui_num(line, n, (int)len);
    n = mus_ui_cat(line, n, "  OCT ");
    if (mus_ui_oct > 0) n = mus_ui_cat(line, n, "+");
    mus_ui_num(line, n, mus_ui_oct);
    draw_text_box(0, 48, 240, &FONT_T, line, C_WHITE, 0);
    n = mus_ui_cat(line, 0, "ARP ");
    n = mus_ui_cat(line, n, mode[mus.arp < 7u ? mus.arp : 0]);
    n = mus_ui_cat(line, n, mus.ahold ? " HOLD  GATE " : "  GATE ");
    mus_ui_num(line, n, mus.agate * 100 / 128);
    draw_text_box(0, 72, 240, &FONT_T, line, C_WHITE, 0);
    for (i = 0; i < 16u; i++) {
        uint32_t step = base + i;
        dots[i] = step >= len ? ' ' : (mus.playing && step == head) ? 'O' : mus_step_used(mus.sel, step) ? 'o' : '.';
    }
    dots[16] = 0;
    draw_text_box(0, 110, 240, &FONT_T, dots, RGB(75, 210, 150), 1);
    n = mus_ui_cat(line, 0, mus.playing ? "PLAY  STEP " : "STOP  STEP ");
    mus_ui_num(line, n, mus.playing ? (int)head + 1 : 0);
    draw_text_box(0, 146, 240, &FONT_T, line, mus.playing ? RGB(40, 220, 90) : C_WHITE, 0);
    draw_text_box(0, 184, 240, &FONT_T, mus_message(), RGB(255, 212, 40), 0);
    mus_ui_seen_step = head;
    mus_ui_seen_play = mus.playing;
    n = 0;
    while (mus_message()[n] && n + 1u < sizeof mus_ui_seen_msg) { mus_ui_seen_msg[n] = mus_message()[n]; n++; }
    mus_ui_seen_msg[n] = 0;
}
static int mus_ui_same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static int mus_ui_moved(void)
{
    uint32_t head = mus.playing ? mus_playhead() : 0;
    return head != mus_ui_seen_step || mus.playing != mus_ui_seen_play || !mus_ui_same(mus_message(), mus_ui_seen_msg);
}
static void mus_ui_turn(unsigned enc, int delta)
{
    if (!delta) return;
    if (enc == EN_SELECT) {
        int t = ((int)mus.sel + delta) % (int)MUS_TRACKS;
        if (t < 0) t += (int)MUS_TRACKS;
        mus_track((uint32_t)t);
    } else if (enc == EN_K1) {
        int bpm = (int)mus.bpm + (delta > 40 ? 40 : delta < -40 ? -40 : delta);
        if (bpm < 40) bpm = 40;
        if (bpm > 240) bpm = 240;
        mus_set_bpm((uint32_t)bpm);
    } else if (enc == EN_K2) {
        int d = (int)mus.div[mus.sel] + (delta > 0 ? 1 : -1);
        if (d < 0) d = 0;
        if (d > 9) d = 9;
        mus_division((uint32_t)d);
    } else if (enc == EN_K3) {
        int g = (int)mus.agate + delta;
        if (g < 1) g = 1;
        if (g > 127) g = 127;
        mus_gate((uint32_t)g);
    } else if (enc == EN_ALGO) {
        int m = (int)mus.arp + (delta > 0 ? 1 : -1);
        if (m < 0) m = 0;
        if (m > 6) m = 6;
        mus_arp_mode((uint32_t)m);
    } else if (enc == EN_PRESET) {
        uint32_t len = mus.len[mus.sel];
        mus_length(len >= 64u ? 16u : len >= 32u ? 64u : 32u);
    }
}
static int mus_ui_note(unsigned key)
{
    int note = 53 + (int)key + 12 * mus_ui_oct;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    return note;
}
