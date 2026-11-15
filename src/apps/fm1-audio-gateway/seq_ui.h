/* SPDX-License-Identifier: GPL-3.0-only
 * Sequencer and arpeggiator pages. SEQ opens NOTES and each further press
 * steps NOTES, STEPS, PATTERN, TEMPO, the same family order as Melodee.
 * NOTES is a 16-step piano roll; four of those pages are 4 bars at 1/16.
 * STEPS is the 4-bar grid. Stopped, on either editor, a key or a chord
 * writes the cursor step and the last release moves the cursor on.
 * PATTERN sets 1, 2 or 4 bars. TEMPO sets BPM. ARP is the arp cards.
 * REC captures. On PATTERN, TEMPO and ARP the keys play, so a stopped
 * empty pattern can still take the Melodee nightly 3fe570d free timing. */
#include "music.h"
static int8_t mus_ui_oct;
static uint8_t mus_ui_page, mus_ui_cursor, mus_ui_held, mus_ui_span = 1, mus_ui_lo = 48;
static uint8_t mus_ui_on[27], mus_ui_pitch[27];
static uint32_t mus_ui_edit_ms, mus_ui_seen;
static uint8_t mus_ui_rec, mus_ui_rec_done, mus_ui_edit, mus_ui_edit_done;
static const char *const mus_ui_modes[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD", "REPEAT"};
static const char *const mus_ui_divs[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
static const uint8_t mus_ui_black[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
enum { MUS_UI_NOTE = 0, MUS_UI_STEPS = 1, MUS_UI_PATTERN = 2, MUS_UI_TEMPO = 3 };
enum { MUS_ROLL_Y = 38, MUS_ROLL_H = 143, MUS_ROLL_RH = 11, MUS_ROLL_ROWS = 13, MUS_ROLL_X = 28, MUS_ROLL_CW = 12 };

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
static int mus_ui_same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static int mus_ui_sign(int delta) { return delta > 0 ? 1 : delta < 0 ? -1 : 0; }
static int mus_ui_clamp(int delta, int lim)
{
    if (delta > lim) return lim;
    if (delta < -lim) return -lim;
    return delta;
}
static uint32_t mus_ui_len(void) { return mus.len[mus.sel] ? mus.len[mus.sel] : 1u; }
static void mus_ui_show(int note)
{
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    if (note < (int)mus_ui_lo) mus_ui_lo = (uint8_t)note;
    if (note > (int)mus_ui_lo + 12) mus_ui_lo = (uint8_t)(note - 12);
}
static void mus_ui_follow(uint32_t step)
{
    uint8_t n;
    if (mus_step_pitch(mus.sel, step, &n)) mus_ui_show((int)n);
}
static void mus_ui_silence(void)
{
    for (unsigned k = 0; k < 27u; k++) if (mus_ui_on[k]) {
        mus_emit(0, mus.sel, mus_ui_pitch[k], 0);
        mus_ui_on[k] = 0;
    }
    mus_ui_held = 0;
    mus_ui_span = 1;
}
static void mus_ui_cycle(void)
{
    mus_ui_silence();
    mus_ui_page = (uint8_t)((mus_ui_page + 1u) % 4u);
}
static int mus_ui_writing(void)
{
    return !mus.playing && mus_ui_page < MUS_UI_PATTERN;
}
static void mus_ui_move(int delta)
{
    int len = (int)mus_ui_len(), c = (int)mus_ui_cursor + delta;
    while (c < 0) c += len;
    mus_ui_cursor = (uint8_t)(c % len);
    mus_ui_follow(mus_ui_cursor);
}
static void mus_ui_extend(int dir)
{
    int span = (int)mus_ui_span + dir;
    uint32_t step = mus_ui_cursor;
    if (span < 1) span = 1;
    if (span > 16) span = 16;
    mus_ui_span = (uint8_t)span;
    for (uint32_t i = 0; i < MUS_REC; i++)
        if (mus.note[mus.sel][i].used && mus.note[mus.sel][i].step == step)
            mus.note[mus.sel][i].dur = (uint32_t)span * MUS_UNIT;
}
static int mus_ui_nudge_pitch(int delta)
{
    int any = 0;
    uint32_t step = mus_ui_cursor;
    for (uint32_t i = 0; i < MUS_REC; i++) {
        mus_note_t *e = &mus.note[mus.sel][i];
        int n;
        if (!e->used || e->step != step) continue;
        n = (int)e->note + delta;
        if (n < 0) n = 0;
        if (n > 127) n = 127;
        e->note = (uint8_t)n;
        mus_ui_show(n);
        any = 1;
    }
    return any;
}
static void mus_ui_nudge_len(int delta)
{
    uint32_t step = mus_ui_cursor;
    for (uint32_t i = 0; i < MUS_REC; i++) {
        mus_note_t *e = &mus.note[mus.sel][i];
        int32_t d;
        if (!e->used || e->step != step) continue;
        d = (int32_t)e->dur + delta * (int32_t)(MUS_UNIT / 4u);
        if (d < (int32_t)(MUS_UNIT / 4u)) d = (int32_t)(MUS_UNIT / 4u);
        if (d > 16 * (int32_t)MUS_UNIT) d = 16 * (int32_t)MUS_UNIT;
        e->dur = (uint32_t)d;
    }
}
static void mus_ui_scroll(int delta)
{
    int lo = (int)mus_ui_lo + delta;
    if (lo < 0) lo = 0;
    if (lo > 115) lo = 115;
    mus_ui_lo = (uint8_t)lo;
}
static void mus_ui_bars(int dir)
{
    uint32_t len = mus.len[mus.sel], bars = len >= 64u ? 4u : len >= 32u ? 2u : 1u;
    if (dir > 0) bars = bars >= 4u ? 4u : bars << 1;
    else if (dir < 0) bars = bars <= 1u ? 1u : bars >> 1;
    mus_length(bars * 16u);
    if (mus_ui_cursor >= mus.len[mus.sel]) mus_ui_cursor = (uint8_t)(mus.len[mus.sel] - 1u);
}
static void mus_ui_division(int dir)
{
    int d = (int)mus.div[mus.sel] + dir;
    if (d < 0) d = 9;
    if (d > 9) d = 0;
    mus_division((uint32_t)d);
}
static void mus_ui_gate_knob(int delta)
{
    int g = (int)mus.agate + delta;
    if (g < 1) g = 1;
    if (g > 127) g = 127;
    mus_gate((uint32_t)g);
}
static void mus_ui_bpm_knob(int delta)
{
    int bpm = (int)mus.bpm + (delta > 40 ? 40 : delta < -40 ? -40 : delta);
    if (bpm < 40) bpm = 40;
    if (bpm > 240) bpm = 240;
    mus_set_bpm((uint32_t)bpm);
}
static void mus_ui_track(int dir)
{
    int t = ((int)mus.sel + dir) % (int)MUS_TRACKS;
    if (t < 0) t += (int)MUS_TRACKS;
    mus_ui_silence();
    mus_track((uint32_t)t);
    if (mus_ui_cursor >= mus_ui_len()) mus_ui_cursor = (uint8_t)(mus_ui_len() - 1u);
}
static void mus_ui_arp_knob(int dir)
{
    int m = (int)mus.arp + dir;
    if (m < 0) m = 0;
    if (m > 6) m = 6;
    mus_arp_mode((uint32_t)m);
}
static void mus_ui_turn(unsigned enc, int delta)
{
    int dir;
    if (!delta) return;
    dir = mus_ui_sign(delta);
    if (enc == EN_SELECT) { mus_ui_track(dir); return; }
    if (enc == EN_ALGO) { mus_ui_arp_knob(dir); return; }
    if (mus_ui_page < MUS_UI_PATTERN) {
        if (enc == EN_K1 || enc == EN_PRESET) {
            if (mus_ui_held) mus_ui_extend(dir);
            else mus_ui_move(mus_ui_clamp(delta, 8));
        } else if (enc == EN_K2) {
            int d = mus_ui_clamp(delta, 12);
            if (!mus_ui_nudge_pitch(d)) mus_ui_scroll(d);
        } else if (enc == EN_K3) mus_ui_nudge_len(mus_ui_clamp(delta, 8));
        return;
    }
    if (mus_ui_page == MUS_UI_PATTERN) {
        if (enc == EN_K1 || enc == EN_PRESET) mus_ui_bars(dir);
        else if (enc == EN_K2) mus_ui_division(dir);
        else if (enc == EN_K3) mus_ui_gate_knob(delta);
        return;
    }
    if (enc == EN_K1) mus_ui_bpm_knob(delta);
    else if (enc == EN_K2) {
        int s = (int)mus.aswing + dir * 10;
        if (s < 0) s = 0;
        if (s > 250) s = 250;
        mus.aswing = (uint8_t)s;
    } else if (enc == EN_K3) mus_ui_gate_knob(delta);
    else if (enc == EN_PRESET) mus_ui_bars(dir);
}
static int mus_ui_note(unsigned key)
{
    int note = 53 + (int)key + 12 * mus_ui_oct;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    return note;
}
static void mus_ui_key(unsigned key, int down)
{
    if (key > 26u) return;
    if (!mus_ui_writing()) {
        mus_note((uint32_t)mus_ui_note(key), 100, down);
        return;
    }
    if (down) {
        int note;
        if (mus_ui_on[key]) return;
        note = mus_ui_note(key);
        if (!mus_ui_held) {
            mus_step_clear(mus.sel, mus_ui_cursor);
            mus_ui_span = 1;
        }
        mus_ui_on[key] = 1;
        mus_ui_pitch[key] = (uint8_t)note;
        mus_ui_held++;
        mus_step_put(mus.sel, mus_ui_cursor, (uint32_t)note, 100, MUS_UNIT);
        mus_emit(1, mus.sel, (uint32_t)note, 100);
        mus_ui_show(note);
    } else if (mus_ui_on[key]) {
        mus_ui_on[key] = 0;
        mus_emit(0, mus.sel, mus_ui_pitch[key], 0);
        if (mus_ui_held) mus_ui_held--;
        if (!mus_ui_held) {
            mus_ui_move((int)mus_ui_span);
            mus_ui_span = 1;
        }
    }
}
static void mus_ui_pitch_name(int note, char *b)
{
    static const char *const nm[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int n = note < 0 ? 0 : note > 127 ? 127 : note;
    int oct = n / 12 - 1;
    unsigned i = 0;
    const char *s = nm[n % 12];
    while (*s && i + 1u < 8u) b[i++] = *s++;
    if (oct < 0) { if (i + 1u < 8u) b[i++] = '-'; }
    else if (i + 1u < 8u) b[i++] = (char)('0' + (oct > 9 ? 9 : oct));
    b[i] = 0;
}
static void mus_ui_head(const char *title, const char *right)
{
    cv_begin(240, 16, C_BLACK);
    cv_text(4, 2, &FONT_T, title, C_WHITE);
    for (int i = 0; i < 4; i++)
        cv_rect(104 + i * 8, 6, 5, 4, i == (int)mus_ui_page ? RGB(75, 210, 150) : RGB(58, 64, 72));
    if (right && right[0])
        cv_text(236 - text_w(&FONT_T, right), 2, &FONT_T, right, mus.playing ? RGB(40, 220, 90) : RGB(75, 210, 150));
    cv_blit(0, 20);
}
static void mus_ui_card(int x, int y, const char *label, const char *value)
{
    cv_begin(112, 32, RGB(16, 20, 26));
    cv_rect(0, 0, 112, 2, RGB(75, 210, 150));
    cv_text(8, 4, &FONT_T, label, RGB(140, 146, 154));
    cv_text(8, 16, &FONT_T, value, C_WHITE);
    cv_blit((uint32_t)x, (uint32_t)y);
}
static void mus_ui_open(int y, int h)
{
    cv_begin(240, (uint32_t)h, C_BLACK);
    cv_oy = -y;
}
static void mus_ui_close(int y)
{
    cv_oy = 0;
    cv_blit(0, (uint32_t)y);
}
static void mus_ui_paint_roll(void)
{
    uint32_t len = mus_ui_len();
    uint32_t focus = mus.playing ? mus_playhead() : mus_ui_cursor;
    uint32_t base = focus / 16u * 16u;
    uint32_t head = mus.playing ? mus_playhead() : 0xFFFFFFFFu;
    int lo = mus_ui_lo > 115 ? 115 : (int)mus_ui_lo;
    uint16_t held = 0;
    for (unsigned k = 0; k < 27u; k++) if (mus_ui_on[k]) {
        int r = lo + 12 - (int)mus_ui_pitch[k];
        if (r >= 0 && r < MUS_ROLL_ROWS) held = (uint16_t)(held | (1u << r));
    }
    for (int band = 0; band < MUS_ROLL_H; band += 16) {
        int h = MUS_ROLL_H - band, y = MUS_ROLL_Y + band;
        if (h > 16) h = 16;
        mus_ui_open(y, h);
        for (int r = 0; r < MUS_ROLL_ROWS; r++) {
            int note = lo + 12 - r, ry = MUS_ROLL_Y + r * MUS_ROLL_RH;
            int black = mus_ui_black[(note % 12 + 12) % 12];
            int hot = (held & (1u << r)) != 0;
            cv_rect(MUS_ROLL_X, ry, 240 - MUS_ROLL_X, MUS_ROLL_RH, black ? RGB(12, 16, 22) : RGB(28, 38, 50));
            cv_rect(0, ry, MUS_ROLL_X - 2, MUS_ROLL_RH, black ? RGB(22, 24, 28) : RGB(214, 218, 224));
            if (black) cv_rect(10, ry, 14, MUS_ROLL_RH, hot ? RGB(40, 120, 90) : RGB(8, 8, 12));
            else if (hot) cv_rect(0, ry, MUS_ROLL_X - 2, MUS_ROLL_RH, RGB(75, 210, 150));
            if ((note % 12) == 0) cv_rect(0, ry + MUS_ROLL_RH - 2, MUS_ROLL_X - 2, 2, RGB(90, 98, 108));
            cv_rect(MUS_ROLL_X, ry + MUS_ROLL_RH - 1, 16 * MUS_ROLL_CW, 1, RGB(48, 58, 70));
            for (int s = 0; s < 16; s++) {
                int alive = base + (uint32_t)s < len;
                uint16_t line = !alive ? RGB(24, 28, 34) : (s % 4) == 0 ? RGB(150, 162, 176) : RGB(64, 76, 90);
                cv_rect(MUS_ROLL_X + s * MUS_ROLL_CW, ry, 1, MUS_ROLL_RH, line);
            }
        }
        if (mus_ui_cursor >= base && mus_ui_cursor < base + 16u && mus_ui_cursor < len) {
            int x = MUS_ROLL_X + (int)(mus_ui_cursor - base) * MUS_ROLL_CW;
            cv_rect(x + 1, MUS_ROLL_Y, MUS_ROLL_CW - 1, MUS_ROLL_H, RGB(36, 58, 72));
        }
        for (uint32_t i = 0; i < MUS_REC; i++) {
            mus_note_t *e = &mus.note[mus.sel][i];
            int col, x, pitch, cols, w, maxw;
            uint16_t color;
            if (!e->used || e->step < base || e->step >= base + 16u || e->step >= len) continue;
            col = (int)(e->step - base);
            x = MUS_ROLL_X + col * MUS_ROLL_CW;
            pitch = e->note;
            color = e->step == mus_ui_cursor ? RGB(186, 255, 214) : RGB(75, 210, 150);
            if (pitch < lo) { cv_rect(x + 1, MUS_ROLL_Y + MUS_ROLL_H - 2, 10, 2, color); continue; }
            if (pitch >= lo + MUS_ROLL_ROWS) { cv_rect(x + 1, MUS_ROLL_Y, 10, 2, color); continue; }
            cols = (int)(e->dur / MUS_UNIT);
            if (e->dur % MUS_UNIT) cols++;
            if (cols < 1) cols = 1;
            w = cols * MUS_ROLL_CW - 2;
            maxw = MUS_ROLL_X + 16 * MUS_ROLL_CW - (x + 1);
            if (w > maxw) w = maxw;
            if (w < 4) w = 4;
            cv_rect(x + 1, MUS_ROLL_Y + (lo + 12 - pitch) * MUS_ROLL_RH + 1, w, MUS_ROLL_RH - 2, color);
        }
        if (mus_ui_cursor >= base && mus_ui_cursor < base + 16u && mus_ui_cursor < len) {
            int x = MUS_ROLL_X + (int)(mus_ui_cursor - base) * MUS_ROLL_CW;
            cv_rect(x, MUS_ROLL_Y, MUS_ROLL_CW, 1, C_WHITE);
            cv_rect(x, MUS_ROLL_Y + MUS_ROLL_H - 1, MUS_ROLL_CW, 1, C_WHITE);
            cv_rect(x, MUS_ROLL_Y, 1, MUS_ROLL_H, C_WHITE);
            cv_rect(x + MUS_ROLL_CW - 1, MUS_ROLL_Y, 1, MUS_ROLL_H, C_WHITE);
        }
        if (head >= base && head < base + 16u && head < len) {
            int x = MUS_ROLL_X + (int)(head - base) * MUS_ROLL_CW + 5;
            cv_rect(x, MUS_ROLL_Y, 2, MUS_ROLL_H, RGB(255, 186, 48));
        }
        mus_ui_close(y);
    }
}
static void mus_ui_grid(void)
{
    uint32_t len = mus_ui_len(), head = mus.playing ? mus_playhead() : 0xFFFFFFFFu;
    for (int band = 0; band < 136; band += 16) {
        int h = 136 - band, y = 40 + band;
        if (h > 16) h = 16;
        mus_ui_open(y, h);
        for (int row = 0; row < 4; row++) {
            int y0 = 40 + row * 34;
            char bar[2];
            bar[0] = (char)('1' + row);
            bar[1] = 0;
            cv_text(2, y0 + 6, &FONT_T, bar, row < (int)((len + 15u) / 16u) ? RGB(140, 146, 154) : RGB(48, 52, 58));
            for (int col = 0; col < 16; col++) {
                uint32_t step = (uint32_t)(row * 16 + col);
                int x = 16 + col * 13, in = step < len, on = in && mus_step_used(mus.sel, step);
                uint16_t edge = in ? ((col % 4) == 0 ? RGB(140, 152, 166) : RGB(78, 88, 102)) : RGB(42, 48, 56);
                cv_rect(x, y0 + 4, 11, 20, on ? (step == mus_ui_cursor ? RGB(186, 255, 214) : RGB(75, 210, 150))
                    : (in ? RGB(24, 32, 42) : RGB(12, 14, 18)));
                cv_rect(x, y0 + 4, 11, 1, edge);
                cv_rect(x, y0 + 23, 11, 1, edge);
                cv_rect(x, y0 + 4, 1, 20, edge);
                cv_rect(x + 10, y0 + 4, 1, 20, edge);
                if (step == mus_ui_cursor && in) {
                    cv_rect(x - 1, y0 + 2, 13, 1, C_WHITE);
                    cv_rect(x - 1, y0 + 24, 13, 1, C_WHITE);
                    cv_rect(x - 1, y0 + 2, 1, 23, C_WHITE);
                    cv_rect(x + 11, y0 + 2, 1, 23, C_WHITE);
                }
                if (step == head) cv_rect(x, y0 + 4, 11, 2, RGB(255, 186, 48));
            }
        }
        mus_ui_close(y);
    }
}
static int mus_ui_quiet(void) { return mus_ui_same(mus_message(), "HOLD REC TO CAPTURE"); }
static uint32_t mus_ui_sig(void);
static void mus_ui_lines(const char *hint)
{
    char line[40], name[8];
    unsigned n = 0;
    uint8_t pitch = 0;
    uint32_t len = mus_ui_len(), bars = (len + 15u) / 16u, bar = mus_ui_cursor / 16u + 1u;
    if (bars < 1u) bars = 1u;
    if (bars > 4u) bars = 4u;
    if (bar > bars) bar = bars;
    if (mus_ui_page < MUS_UI_PATTERN && mus_step_pitch(mus.sel, mus_ui_cursor, &pitch)) mus_ui_pitch_name((int)pitch, name);
    else if (mus_ui_page < MUS_UI_PATTERN) { name[0] = 'R'; name[1] = 'E'; name[2] = 'S'; name[3] = 'T'; name[4] = 0; }
    else name[0] = 0;
    if (name[0]) {
        n = mus_ui_cat(line, 0, name);
        n = mus_ui_cat(line, n, "  BAR ");
        n = mus_ui_num(line, n, (int)bar);
        n = mus_ui_cat(line, n, "/");
        mus_ui_num(line, n, (int)bars);
    } else line[0] = 0;
    draw_text_box(0, 186, 240, &FONT_T, line[0] ? line : hint, line[0] ? C_WHITE : RGB(170, 176, 184), 0);
    draw_text_box(0, 202, 240, &FONT_T, mus_ui_quiet() ? (line[0] ? hint : "") : mus_message(),
        mus_ui_quiet() ? RGB(140, 146, 154) : RGB(255, 212, 40), 0);
}
static void mus_ui_gate_text(char *b)
{
    unsigned n = mus_ui_num(b, 0, (int)(mus.agate * 100u / 128u));
    mus_ui_cat(b, n, "%");
}
static void mus_ui_draw(void)
{
    char right[16], value[16], gate[16];
    unsigned n;
    static const char *const title[] = {"NOTES", "STEPS", "PATTERN", "TEMPO"};
    if (mus_ui_page == MUS_UI_NOTE && mus.playing) mus_ui_follow(mus_playhead());
    n = mus_ui_num(right, 0, (int)mus_ui_cursor + 1);
    n = mus_ui_cat(right, n, "/");
    mus_ui_num(right, n, (int)mus_ui_len());
    if (mus_ui_page >= MUS_UI_PATTERN) {
        right[0] = 'T';
        mus_ui_num(right, 1, (int)mus.sel + 1);
    }
    mus_ui_head(title[mus_ui_page < 4u ? mus_ui_page : 0], right);
    mus_ui_gate_text(gate);
    if (mus_ui_page == MUS_UI_NOTE) {
        mus_ui_paint_roll();
        mus_ui_lines(mus.playing ? "KEYS PLAY" : "K1 STEP  K2 PITCH  K3 LENGTH");
    } else if (mus_ui_page == MUS_UI_STEPS) {
        mus_ui_grid();
        mus_ui_lines(mus.playing ? "KEYS PLAY" : "KEY WRITES  RELEASE ADVANCES");
    } else if (mus_ui_page == MUS_UI_PATTERN) {
        uint32_t bars = mus.len[mus.sel] >= 64u ? 4u : mus.len[mus.sel] >= 32u ? 2u : 1u;
        n = mus_ui_num(value, 0, (int)bars);
        mus_ui_cat(value, n, bars == 1u ? " BAR" : " BARS");
        mus_ui_card(8, 48, "BARS", value);
        mus_ui_card(120, 48, "DIV", mus_ui_divs[mus.div[mus.sel] < 10u ? mus.div[mus.sel] : 2u]);
        mus_ui_card(8, 96, "GATE", gate);
        n = mus_ui_num(value, 0, (int)mus_ui_len());
        mus_ui_cat(value, n, " STEPS");
        mus_ui_card(120, 96, "LENGTH", value);
        draw_text_box(0, 146, 240, &FONT_T, "16 STEPS A BAR, UP TO 4", RGB(140, 146, 154), 1);
        mus_ui_lines("K1 BARS  K2 DIV  K3 GATE");
    } else {
        mus_ui_num(value, 0, (int)mus.bpm);
        mus_ui_card(8, 48, "BPM", value);
        mus_ui_num(value, 0, (int)mus.aswing);
        mus_ui_card(120, 48, "SWING", value);
        mus_ui_card(8, 96, "GATE", gate);
        mus_ui_card(120, 96, "ARP", mus_ui_modes[mus.arp < 7u ? mus.arp : 0]);
        draw_text_box(0, 146, 240, &FONT_T, mus.playing ? "PLAY" : "STOP", mus.playing ? RGB(40, 220, 90) : C_WHITE, 1);
        mus_ui_lines("K1 BPM  K2 SWING  K3 GATE");
    }
    mus_ui_seen = mus_ui_sig();
}
static uint32_t mus_ui_sig(void)
{
    uint32_t s = mus_ui_page * 131u + mus_ui_cursor * 17u + mus_ui_lo + (uint32_t)mus.playing * 9u + mus.sel * 3u;
    s = s * 33u + (mus.playing ? mus_playhead() : 0u);
    s = s * 33u + mus.bpm + mus_ui_len() * 64u + mus.div[mus.sel] * 8u;
    s = s * 33u + mus.arp + mus.agate + mus.aswing + mus.arp_div + mus.aoct + mus.ahold + mus_ui_held;
    for (uint32_t i = 0; i < MUS_REC; i++) if (mus.note[mus.sel][i].used) {
        mus_note_t *e = &mus.note[mus.sel][i];
        s = s * 33u + e->note + (uint32_t)e->step * 128u + (e->dur >> 8);
    }
    for (const char *p = mus_message(); *p; p++) s = s * 33u + (uint8_t)*p;
    return s ? s : 1u;
}
static int mus_ui_moved(void) { return mus_ui_sig() != mus_ui_seen; }
static void mus_ui_draw_arp(void)
{
    char value[16], gate[16];
    unsigned div = mus.arp_div < 10u ? mus.arp_div : 2u;
    cv_begin(240, 16, C_BLACK);
    cv_text(4, 2, &FONT_T, "ARP", C_WHITE);
    value[0] = 'T';
    mus_ui_num(value, 1, (int)mus.sel + 1);
    cv_text(236 - text_w(&FONT_T, value), 2, &FONT_T, value, mus.playing ? RGB(40, 220, 90) : RGB(75, 210, 150));
    cv_blit(0, 20);
    {
        char mode[20];
        unsigned k = mus_ui_cat(mode, 0, mus_ui_modes[mus.arp < 7u ? mus.arp : 0]);
        if (mus.ahold) mus_ui_cat(mode, k, " HOLD");
        mus_ui_card(8, 48, "MODE", mode);
    }
    mus_ui_card(120, 48, "RATE", mus_ui_divs[div]);
    mus_ui_num(value, 0, mus.aoct);
    mus_ui_card(8, 96, "OCT", value);
    mus_ui_gate_text(gate);
    mus_ui_card(120, 96, "GATE", gate);
    draw_text_box(0, 146, 240, &FONT_T, "KEYS ARP TO MIDI", RGB(75, 210, 150), 1);
    draw_text_box(0, 186, 240, &FONT_T, "ALGO MODE  K1 RATE  K2 OCT  K3 GATE", RGB(140, 146, 154), 0);
    draw_text_box(0, 202, 240, &FONT_T, mus_ui_quiet() ? (mus.playing ? "PLAY" : "STOP") : mus_message(),
        mus_ui_quiet() ? (mus.playing ? RGB(40, 220, 90) : C_WHITE) : RGB(255, 212, 40), 0);
    mus_ui_seen = mus_ui_sig();
}
static void mus_ui_turn_arp(unsigned enc, int delta)
{
    int dir;
    if (!delta) return;
    dir = mus_ui_sign(delta);
    if (enc == EN_SELECT) mus_ui_track(dir);
    else if (enc == EN_ALGO) mus_ui_arp_knob(dir);
    else if (enc == EN_K1) {
        int d = (int)mus.arp_div + dir;
        if (d < 0) d = 0;
        if (d > 9) d = 9;
        mus.arp_div = (uint8_t)d;
    } else if (enc == EN_K2) {
        int o = (int)mus.aoct + dir;
        if (o < 1) o = 1;
        if (o > 4) o = 4;
        mus_octaves((uint32_t)o);
    } else if (enc == EN_K3) mus_ui_gate_knob(delta);
}
