/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Fminterface
 * Retrospective capture, tempo detection and arpeggiator step order are derived
 * from Melodee nightly 3fe570d (https://github.com/keremimo/melodee),
 * Copyright (C) 2026 Kerem Kilic (Ellic Studio). Portions of the arpeggiator
 * follow Felucca, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hugelton Instruments.
 *
 * MIDI sequencer for the audio interface. Time is musical units: a quarter note
 * is MUS_BEAT units, the same grid as that nightly build, so a free take lands
 * on the same steps and the same BPM. The audio path does not call in here.
 */
#ifndef FM_INTERFACE_MUSIC_H
#define FM_INTERFACE_MUSIC_H
#include <stdint.h>
#ifdef PLAT_HOST
#include <string.h>
#endif
#define MUS_TRACKS 4u
#define MUS_CAP 128u
#define MUS_REC 96u
#define MUS_BEAT 24576u
#define MUS_REST_MS 8000u
#define MUS_BPM_MIN 40u
#define MUS_BPM_MAX 240u
#define MUS_UNIT 65536u
/* w steps at this many units per step: milliseconds times BPM. Matches nightly
 * CAP_LOOP_BPM_MS, including the order of the integer divisions. */
#define MUS_LOOP_BPM_MS(w, step) ((((w) * (step) / 96u) * 15000u) / 64u)
typedef struct { uint16_t step, dur; uint8_t on, note, vel, tr; } mus_cap_t;
typedef struct { uint8_t note, vel, step, used; uint16_t on; uint32_t dur; } mus_note_t;
static struct {
    uint32_t ms, rem, pos, bpm, channel;
    uint32_t cap_w, cap_from, free_t0, free_last;
    uint32_t arp_pos, arp_idx, arp_off;
    uint16_t len[MUS_TRACKS];
    uint8_t div[MUS_TRACKS];
    uint8_t sel, playing, just, free, clock_ext, armed, clock_set;
    uint8_t arp, ahold, aoct, agate, aswing, arp_div, live_arp;
    uint32_t clock_pos;
    uint8_t nheld, phys, arp_have, arp_note, live_n;
    uint8_t held[16], live[16];
    uint8_t seq_on[MUS_TRACKS][MUS_REC];
    mus_note_t note[MUS_TRACKS][MUS_REC];
    mus_cap_t cap[MUS_CAP];
    struct { uint8_t valid, track, playing; uint16_t len, bpm, n; mus_note_t note[MUS_REC]; } undo;
    uint32_t mq[64];
    uint8_t mq_r, mq_n;
    char msg[40];
} mus;
static uint32_t mus_step_units(uint32_t div)
{
    static const uint8_t den[6] = {1, 2, 4, 8, 3, 6};
    if (div < 6u) return MUS_BEAT / den[div];
    if (div < 10u) return MUS_BEAT << (div - 5u);
    return MUS_BEAT / den[div % 6u];
}
static unsigned mus_put(char *d, unsigned n, const char *s)
{
    while (*s && n + 1u < 40u) d[n++] = *s++;
    d[n] = 0;
    return n;
}
static unsigned mus_put_u(char *d, unsigned n, uint32_t v)
{
    char tmp[8];
    unsigned k = 0;
    do { tmp[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 8u);
    while (k && n + 1u < 40u) d[n++] = tmp[--k];
    d[n] = 0;
    return n;
}
static void mus_say(const char *s) { mus_put(mus.msg, 0, s); }
static uint32_t mus_rng_state = 1;
static uint32_t mus_rng(void) { mus_rng_state = mus_rng_state * 1664525u + 1013904223u; return mus_rng_state; }
static void mus_push(uint32_t pkt)
{
    if (mus.mq_n >= 64u) { mus.mq_r = (uint8_t)((mus.mq_r + 1u) & 63u); mus.mq_n--; }
    mus.mq[(mus.mq_r + mus.mq_n) & 63u] = pkt;
    mus.mq_n++;
}
static uint32_t mus_packet(unsigned on, uint32_t ch, uint32_t note, uint32_t vel)
{
    unsigned status = (on ? 0x90u : 0x80u) | (ch & 15u);
    return (on ? 9u : 8u) | status << 8 | (note & 127u) << 16 | (vel & 127u) << 24;
}
static void mus_emit(unsigned on, uint32_t track, uint32_t note, uint32_t vel)
{
    if (note > 127u) return;
    mus_push(mus_packet(on, mus.channel + track, note, on ? (vel ? vel : 1u) : 0));
}
static int mus_midi_peek(uint32_t *pkt)
{
    if (!mus.mq_n) return 0;
    *pkt = mus.mq[mus.mq_r];
    return 1;
}
static void mus_midi_drop(void)
{
    if (!mus.mq_n) return;
    mus.mq_r = (uint8_t)((mus.mq_r + 1u) & 63u);
    mus.mq_n--;
}
static int mus_midi(uint32_t *pkt)
{
    if (!mus_midi_peek(pkt)) return 0;
    mus_midi_drop();
    return 1;
}
static uint32_t mus_count(uint32_t track)
{
    uint32_t n = 0, i;
    for (i = 0; i < MUS_REC; i++) n += mus.note[track][i].used;
    return n;
}
static int mus_other_notes(uint32_t track)
{
    uint32_t k;
    for (k = 0; k < MUS_TRACKS; k++) if (k != track && mus_count(k)) return 1;
    return 0;
}
static void mus_live_silence(void)
{
    uint32_t i;
    for (i = 0; i < mus.live_n; i++) mus_emit(0, mus.sel, mus.live[i], 0);
    mus.live_n = 0;
}
static void mus_cap_off(uint32_t note);
static void mus_arp_silence(void)
{
    if (!mus.arp_have) return;
    mus_cap_off(mus.arp_note);
    mus_emit(0, mus.sel, mus.arp_note, 0);
    mus.arp_have = 0;
}
static void mus_seq_silence(void)
{
    uint32_t k, i;
    for (k = 0; k < MUS_TRACKS; k++) for (i = 0; i < MUS_REC; i++) if (mus.seq_on[k][i]) {
        mus_emit(0, k, mus.note[k][i].note, 0);
        mus.seq_on[k][i] = 0;
    }
}
static void mus_rt(uint32_t status)
{
    mus_push(0x0Fu | (status << 8));
}
static void mus_stop(void)
{
    if (!mus.playing) { mus_arp_silence(); return; }
    mus.playing = 0;
    mus.just = 0;
    mus_seq_silence();
    mus_rt(0xFCu);
}
static void mus_play(int on)
{
    if (!on) { mus_stop(); return; }
    if (mus.playing) return;
    mus.playing = 1;
    mus.pos = 0;
    mus.just = 1;
    mus.clock_pos = 0;
    mus.cap_from = mus.cap_w;
    mus.free = 0;
    memset(mus.seq_on, 0, sizeof mus.seq_on);
    mus_rt(0xFAu);
}
static uint32_t mus_now_q(uint32_t track)
{
    uint32_t su = mus_step_units(mus.div[track]), step, frac;
    step = su ? mus.pos / su : 0;
    frac = su ? (mus.pos % su) * 256u / su : 0;
    if (frac > 255u) frac = 255u;
    return (step << 8) + frac;
}
static void mus_cap_on(uint32_t note, uint32_t vel)
{
    mus_cap_t *e;
    uint32_t k = mus.sel;
    if (!mus.playing) {
        if (mus.armed || mus.clock_ext || mus_count(k)) return;
        if (!mus.free || mus.ms - mus.free_last > MUS_REST_MS || mus.ms - mus.free_t0 > 65535u) {
            mus.free = 1;
            mus.free_t0 = mus.ms;
            mus.cap_from = mus.cap_w;
        }
        mus.free_last = mus.ms;
        e = &mus.cap[mus.cap_w % MUS_CAP];
        e->step = (uint16_t)(mus.ms - mus.free_t0);
        e->on = 0;
    } else {
        uint32_t q = mus_now_q(k);
        e = &mus.cap[mus.cap_w % MUS_CAP];
        e->step = (uint16_t)(q >> 8);
        e->on = (uint8_t)q;
        mus.free = 0;
    }
    e->dur = 0;
    e->note = (uint8_t)note;
    e->vel = (uint8_t)(vel ? vel : 1u);
    e->tr = (uint8_t)(k | 128u);
    mus.cap_w++;
}
static uint32_t mus_cap_len(const mus_cap_t *e, uint32_t now)
{
    uint32_t d = (e->tr & 128u) ? (now - ((uint32_t)e->step << 8) - e->on) & 0xFFFFFFu : e->dur;
    if (d < 1u) d = 1u;
    if (d > 65535u) d = 65535u;
    return d;
}
static void mus_cap_off(uint32_t note)
{
    uint32_t i, k = mus.sel;
    for (i = 0; i < mus.cap_w - mus.cap_from && i < MUS_CAP; i++) {
        mus_cap_t *e = &mus.cap[(mus.cap_w - 1u - i) % MUS_CAP];
        if (e->tr == (k | 128u) && e->note == note) {
            if (mus.free && !mus.playing) {
                uint32_t ms = mus.ms - mus.free_t0 - e->step;
                e->dur = (uint16_t)(ms > 65535u ? 65535u : ms);
            } else e->dur = (uint16_t)mus_cap_len(e, mus_now_q(k));
            e->tr = (uint8_t)k;
        }
    }
}
static void mus_live_add(uint32_t note)
{
    uint32_t i;
    for (i = 0; i < mus.live_n; i++) if (mus.live[i] == note) return;
    if (mus.live_n < 16u) mus.live[mus.live_n++] = (uint8_t)note;
}
static void mus_live_drop(uint32_t note)
{
    uint32_t i, n = 0;
    for (i = 0; i < mus.live_n; i++) if (mus.live[i] != note) mus.live[n++] = mus.live[i];
    mus.live_n = (uint8_t)n;
}
static void mus_arp_add(uint32_t note)
{
    uint32_t i;
    if (mus.ahold && mus.phys == 0) mus.nheld = 0;
    mus.phys++;
    for (i = 0; i < mus.nheld; i++) if (mus.held[i] == note) return;
    if (mus.nheld < 16u) mus.held[mus.nheld++] = (uint8_t)note;
    if (mus.nheld == 1u) { mus.arp_pos = 0xFFFFFFF; mus.arp_idx = 0xFFFFFFFFu; }
}
static void mus_arp_remove(uint32_t note)
{
    uint32_t i, n = 0;
    if (mus.phys) mus.phys--;
    if (mus.ahold) return;
    for (i = 0; i < mus.nheld; i++) if (mus.held[i] != note) mus.held[n++] = mus.held[i];
    mus.nheld = (uint8_t)n;
}
static void mus_note(uint32_t note, uint32_t vel, int on)
{
    if (note > 127u) return;
    if (mus.arp) {
        if (on && vel) mus_arp_add(note);
        else mus_arp_remove(note);
        return;
    }
    if (on && vel) {
        mus_cap_on(note, vel);
        mus_emit(1, mus.sel, note, vel);
        mus_live_add(note);
    } else {
        mus_cap_off(note);
        mus_emit(0, mus.sel, note, 0);
        mus_live_drop(note);
    }
}
static void mus_arp_advance(uint32_t n)
{
    uint32_t period, sw, list[64], len, cnt, i, j, o;
    if (!n) return;
    if (mus.arp_have) {
        if (mus.arp_off <= n) mus_arp_silence();
        else mus.arp_off -= n;
    }
    unsigned mode = mus.arp;
    if (!mode || !mus.nheld) {
        if (!mus.nheld && mus.arp_have) mus_arp_silence();
        return;
    }
    period = mus_step_units(mus.arp_div < 10u ? mus.arp_div : 2u);
    sw = (uint32_t)mus.aswing * period / 250u;
    mus.arp_pos += n;
    for (;;) {
        /* Even steps are shorter by the swing, odd steps longer, as in Melodee.
         * Zero swing keeps every step exactly one division. */
        int32_t adj = (mus.arp_idx & 1u) ? (int32_t)sw : -(int32_t)sw;
        uint32_t limit = (uint32_t)((int32_t)period + adj);
        if (mus.arp_pos < limit && mus.arp_pos != 0xFFFFFFFu + n) return;
        if (mus.arp_pos >= 0xFFFFFFFu) mus.arp_pos = 0;
        else mus.arp_pos -= limit;
        len = 0;
        for (i = 0; i < mus.nheld; i++) list[i] = mus.held[i];
        cnt = mus.nheld;
        if (mode != 5u) for (i = 1; i < cnt; i++) for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
            uint32_t x = list[j]; list[j] = list[j - 1]; list[j - 1] = x;
        }
        for (o = 0; o < mus.aoct; o++) for (i = 0; i < cnt && len < 64u; i++) {
            uint32_t pitched = list[i] + 12u * o;
            if (pitched < 128u) list[len++] = pitched;
        }
        if (!len) return;
        if (mode == 6u) { list[0] = mus.held[mus.nheld - 1u]; len = 1; }
        mus.arp_idx++;
        switch (mode) {
        case 2: j = len - 1u - mus.arp_idx % len; break;
        case 3: {
            uint32_t cyc = len > 1u ? 2u * len - 2u : 1u, p = mus.arp_idx % cyc;
            j = p < len ? p : cyc - p;
            break;
        }
        case 4: j = mus_rng() % len; break;
        default: j = mus.arp_idx % len; break;
        }
        if (mus.arp_have) mus_arp_silence();
        mus.arp_note = (uint8_t)list[j];
        mus.arp_have = 1;
        mus.arp_off = period * (mus.agate ? mus.agate : 1u) / 128u;
        if (!mus.arp_off) mus.arp_off = 1;
        mus_cap_on(mus.arp_note, 100);
        mus_emit(1, mus.sel, mus.arp_note, 100);
        if (mus.arp_off <= mus.arp_pos) mus_arp_silence();
        else mus.arp_off -= mus.arp_pos;
    }
}
static int mus_hit(uint32_t a, uint32_t b, uint32_t point) { return a < point && point <= b; }
static void mus_seq_track(uint32_t k, uint32_t prev, uint32_t now)
{
    uint32_t su = mus_step_units(mus.div[k]), len = mus.len[k] ? mus.len[k] : 1u, L = su * len;
    if (!L) return;
    while (prev < now) {
        uint32_t base = prev - prev % L, next = now < base + L ? now : base + L, a = prev - base, b = next - base, i;
        int just = mus.just && a == 0;
        for (i = 0; i < MUS_REC; i++) if (mus.note[k][i].used) {
            mus_note_t *e = &mus.note[k][i];
            uint32_t start = (uint32_t)e->step * su + (uint32_t)((uint64_t)e->on * su / MUS_UNIT);
            uint32_t gate = (uint32_t)((uint64_t)e->dur * su / MUS_UNIT);
            uint32_t end;
            if (!gate) gate = 1;
            end = start + gate;
            if (end > L) end = L;
            if (((just && start == 0) || mus_hit(a, b, start)) && !mus.seq_on[k][i]) {
                mus.seq_on[k][i] = 1;
                mus_emit(1, k, e->note, e->vel);
            }
            if (mus_hit(a, b, end) && mus.seq_on[k][i]) {
                mus.seq_on[k][i] = 0;
                mus_emit(0, k, e->note, 0);
            }
        }
        prev = next;
    }
}
static void mus_advance(uint32_t dms)
{
    uint32_t prev, units;
    uint64_t ticks;
    if (!dms) return;
    if (dms > 1000u && mus.playing) dms = 1000u;
    ticks = (uint64_t)dms * mus.bpm * MUS_BEAT + mus.rem;
    mus.rem = (uint32_t)(ticks % 60000u);
    units = (uint32_t)(ticks / 60000u);
    prev = mus.pos;
    if (mus.playing && units) {
        mus.pos = prev + units;
        mus.clock_pos += units;
        while (mus.clock_pos >= MUS_BEAT / 24u) {
            mus.clock_pos -= MUS_BEAT / 24u;
            mus_rt(0xF8u);
        }
        for (uint32_t k = 0; k < MUS_TRACKS; k++) mus_seq_track(k, prev, mus.pos);
        mus.just = 0;
    }
    mus_arp_advance(units);
}
static void mus_time(uint32_t ms)
{
    uint32_t prev = mus.ms;
    mus.clock_set = 1;
    if (ms < prev) { mus.ms = ms; return; }
    mus.ms = ms;
    mus_advance(ms - prev);
}
static void mus_poll(uint32_t now)
{
    if (!mus.clock_set) { mus.ms = now; mus.clock_set = 1; return; }
    if (now < mus.ms) { mus.ms = now; return; }
    mus_time(now);
}
static void mus_undo_save(uint32_t track)
{
    mus.undo.valid = 1;
    mus.undo.track = (uint8_t)track;
    mus.undo.playing = mus.playing;
    mus.undo.len = mus.len[track];
    mus.undo.bpm = (uint16_t)mus.bpm;
    mus.undo.n = (uint16_t)mus_count(track);
    memcpy(mus.undo.note, mus.note[track], sizeof mus.undo.note);
}
static int mus_add(uint32_t track, uint32_t step, uint32_t on, uint32_t dur, uint32_t note, uint32_t vel)
{
    uint32_t i;
    if (step > 63u) return 0;
    for (i = 0; i < MUS_REC; i++) {
        mus_note_t *e = &mus.note[track][i];
        if (e->used && e->note == note && e->step == step && e->on == on) return 1;
    }
    for (i = 0; i < MUS_REC; i++) if (!mus.note[track][i].used) {
        mus.note[track][i] = (mus_note_t){(uint8_t)note, (uint8_t)(vel ? vel : 1u), (uint8_t)step, 1, (uint16_t)on, dur};
        return 1;
    }
    return 0;
}
static void mus_store_dur(uint32_t raw, uint32_t *dur)
{
    uint32_t exp = 0;
    while (raw > 65535u && exp < 7u) { raw >>= 1; exp++; }
    *dur = raw << exp;
}
static void mus_say_bars(uint32_t w, uint32_t bpm, int free_take, int full)
{
    unsigned n = mus_put(mus.msg, 0, full ? "FULL: " : "CAPTURED ");
    if (w % 16u) {
        n = mus_put_u(mus.msg, n, w);
        n = mus_put(mus.msg, n, " STEPS");
    } else {
        n = mus_put_u(mus.msg, n, w / 16u);
        n = mus_put(mus.msg, n, w > 16u ? " BARS" : " BAR");
    }
    if (free_take) {
        n = mus_put(mus.msg, n, " ");
        n = mus_put_u(mus.msg, n, bpm);
        mus_put(mus.msg, n, " BPM");
    }
}
/* Nightly free take: the press is the downbeat. 16, 32 or 64 steps, whichever
 * BPM is nearest the current one. Within 3% the current BPM stays. Another
 * track's notes, one note, or a late press keep the tempo and the played times. */
static uint32_t mus_free_place(mus_cap_t *ev, uint32_t from, uint32_t until, uint32_t span, uint32_t *bpm, int keep, uint32_t step_u, uint32_t track)
{
    uint32_t cur = mus.bpm, w = 0, best = 0, gap = 0xFFFFFFFFu, last = 0, n = 0, i, press;
    if (span - 1u >= 0x7FFFFFFFu) return 0;
    press = span = span > 65535u ? 65535u : span;
    for (i = from; i != until; i++) if (ev[i % MUS_CAP].step < press) {
        n++;
        if (ev[i % MUS_CAP].step > last) last = ev[i % MUS_CAP].step;
    }
    if (!n || press - last > MUS_REST_MS) return 0;
    if (!keep && n > 1u && press - last <= press / 4u * 3u) for (uint32_t c = 16u; c <= 64u; c *= 2u) {
        uint32_t b = (MUS_LOOP_BPM_MS(c, step_u) + press / 2u) / press, d = b > cur ? b - cur : cur - b;
        if (b >= MUS_BPM_MIN && b <= MUS_BPM_MAX && d < gap) { gap = d; best = b; w = c; }
    }
    if (w) *bpm = gap * 100u <= cur * 3u ? cur : best;
    else {
        for (w = 16u; w < 64u && MUS_LOOP_BPM_MS(w, step_u) / cur <= last; w *= 2u) ;
        span = MUS_LOOP_BPM_MS(w, step_u) / cur;
        if (!span) return 0;
        *bpm = cur;
    }
    for (i = from; i != until; i++) {
        mus_cap_t *e = &ev[i % MUS_CAP];
        uint32_t at = e->step, pos = at * w * 256u / span;
        uint32_t held = (e->tr & 128u) ? (press > at ? press - at : 1u) : e->dur;
        uint32_t len = held * w * 256u / span;
        e->step = (uint16_t)(pos >> 8);
        e->on = (uint8_t)pos;
        e->dur = (uint16_t)(len > 65535u ? 65535u : len < 1u ? 1u : len);
        e->tr = at < press && at < span ? (uint8_t)track : 255u;
    }
    return w;
}
static int mus_commit(uint32_t track, mus_cap_t *ev, uint32_t from, uint32_t until, uint32_t start, uint32_t w, uint32_t now, int already)
{
    uint32_t i, put = 0;
    int saved = 0, full = 0;
    for (i = from; i != until; i++) {
        mus_cap_t *e = &ev[i % MUS_CAP];
        uint32_t actual = already ? e->step : (uint16_t)(e->step - start);
        uint32_t on, raw, dur, seen, slot;
        if ((e->tr & 127u) != track || actual >= w) continue;
        on = (uint32_t)e->on << 8;
        raw = (already ? e->dur : mus_cap_len(e, now)) << 8;
        mus_store_dur(raw, &dur);
        for (seen = 0; seen < MUS_REC; seen++) {
            mus_note_t *n = &mus.note[track][seen];
            if (n->used && n->note == e->note && n->step == actual && n->on == (uint16_t)on) break;
        }
        if (seen < MUS_REC) continue;
        for (slot = 0; slot < MUS_REC && mus.note[track][slot].used; slot++) ;
        if (slot == MUS_REC) { full = 1; break; }
        if (!saved) {
            mus_undo_save(track);
            if (!mus_count(track)) mus.len[track] = (uint16_t)w;
            saved = 1;
        }
        mus.note[track][slot] = (mus_note_t){(uint8_t)e->note, e->vel ? e->vel : 1u, (uint8_t)actual, 1, (uint16_t)on, dur};
        put++;
    }
    return put ? (full ? 2 : 1) : 0;
}
static int mus_capture_at(uint32_t press_ms)
{
    static mus_cap_t ev[MUS_CAP];
    uint32_t k = mus.sel, from, until, bpm = mus.bpm, w = 0, step_u = mus_step_units(mus.div[k]);
    int free_take, keep, empty, started, wrote;
    if (mus.armed) { mus_say("NOTHING TO CAPTURE"); return 0; }
    memcpy(ev, mus.cap, sizeof ev);
    from = mus.cap_w - mus.cap_from > MUS_CAP ? mus.cap_w - MUS_CAP : mus.cap_from;
    until = mus.cap_w;
    free_take = mus.free && !mus.playing;
    empty = !mus_count(k);
    if (free_take) {
        uint32_t span = press_ms >= mus.free_t0 ? press_ms - mus.free_t0 : 0;
        keep = mus_other_notes(k);
        w = empty ? mus_free_place(ev, from, until, span, &bpm, keep, step_u, k) : 0;
        if (!w) { mus_say("NOTHING TO CAPTURE"); return 0; }
        wrote = mus_commit(k, ev, from, until, 0, w, w << 8, 1);
    } else if (mus.playing) {
        uint32_t now = mus_now_q(k), n = 0, i, first = 0xFFFFu, last = 0, cur, pos, end, span, start;
        for (i = from; i != until; i++) {
            uint32_t back = (uint16_t)((now >> 8) - ev[i % MUS_CAP].step);
            if ((ev[i % MUS_CAP].tr & 127u) != k || back >= 64u) continue;
            n++;
            if (back > last) last = back;
            if (back < first) first = back;
        }
        if (!n) { mus_say("NOTHING TO CAPTURE"); return 0; }
        cur = (now >> 8) & 0xFFFFu;
        pos = cur & 15u;
        end = (uint16_t)(cur - pos + (first <= pos ? 16u : 0u));
        span = (uint16_t)(end - (uint16_t)(cur - last));
        w = empty ? (span <= 16u ? 16u : span <= 32u ? 32u : 64u) : mus.len[k];
        start = (uint16_t)(end - w);
        wrote = mus_commit(k, ev, from, until, start, w, now, 0);
        if (!wrote) { mus_say("NOTHING TO CAPTURE"); return 0; }
    } else { mus_say("NOTHING TO CAPTURE"); return 0; }
    if (!wrote) { mus_say("NOTHING TO CAPTURE"); return 0; }
    started = !mus.playing;
    if (free_take) {
        mus.bpm = bpm;
        mus_play(1);
    }
    mus_say_bars(w, mus.bpm, free_take, wrote == 2);
    (void)started;
    return 1;
}
static int mus_undo(void)
{
    uint32_t k;
    if (!mus.undo.valid) return 0;
    k = mus.undo.track;
    mus_seq_silence();
    memcpy(mus.note[k], mus.undo.note, sizeof mus.undo.note);
    mus.len[k] = mus.undo.len;
    mus.bpm = mus.undo.bpm;
    if (mus.playing && !mus.undo.playing) mus_stop();
    mus.undo.valid = 0;
    mus_say("CAPTURE UNDONE");
    return 1;
}
static void mus_init(void)
{
    memset(&mus, 0, sizeof mus);
    mus.bpm = 120;
    mus.aoct = 1;
    mus.agate = 64;
    mus.arp_div = 2;
    mus_rng_state = 1;
    for (uint32_t k = 0; k < MUS_TRACKS; k++) { mus.len[k] = 16; mus.div[k] = 2; }
    mus_say("HOLD REC TO CAPTURE");
}
static void mus_set_bpm(uint32_t bpm)
{
    if (bpm < MUS_BPM_MIN) bpm = MUS_BPM_MIN;
    if (bpm > MUS_BPM_MAX) bpm = MUS_BPM_MAX;
    mus.bpm = bpm;
}
static void mus_track(uint32_t track)
{
    if (track >= MUS_TRACKS) track = MUS_TRACKS - 1u;
    if (track == mus.sel) return;
    mus_live_silence();
    mus_arp_silence();
    mus.nheld = mus.phys = 0;
    mus.sel = (uint8_t)track;
}
static void mus_length(uint32_t len)
{
    if (len < 1u) len = 1u;
    if (len > 64u) len = 64u;
    mus.len[mus.sel] = (uint16_t)len;
}
static void mus_division(uint32_t div) { if (div < 10u) mus.div[mus.sel] = (uint8_t)div; }
static void mus_arp_mode(uint32_t mode)
{
    if (mode > 6u) mode = 6u;
    if (mode && !mus.arp) mus_live_silence();
    if (!mode && mus.arp) { mus_arp_silence(); mus.nheld = mus.phys = 0; }
    mus.arp = (uint8_t)mode;
}
static void mus_release_keys(void)
{
    mus_live_silence();
    if (!mus.ahold) { mus_arp_silence(); mus.nheld = mus.phys = 0; }
}
static int mus_find(uint32_t track, uint32_t pitch, uint8_t *step, uint16_t *on, uint32_t *dur)
{
    for (uint32_t i = 0; i < MUS_REC; i++) if (mus.note[track][i].used && mus.note[track][i].note == pitch) {
        if (step) *step = mus.note[track][i].step;
        if (on) *on = mus.note[track][i].on;
        if (dur) *dur = mus.note[track][i].dur;
        return 1;
    }
    return 0;
}
static void mus_seed(uint32_t track, uint32_t step, uint32_t note)
{
    if (track >= MUS_TRACKS) return;
    mus_add(track, step, 0, MUS_UNIT, note, 100);
}
static int mus_step_used(uint32_t track, uint32_t step)
{
    for (uint32_t i = 0; i < MUS_REC; i++) if (mus.note[track][i].used && mus.note[track][i].step == step) return 1;
    return 0;
}
static uint32_t mus_playhead(void)
{
    uint32_t su = mus_step_units(mus.div[mus.sel]);
    return su ? (mus.pos / su) % (mus.len[mus.sel] ? mus.len[mus.sel] : 1u) : 0;
}
static const char *mus_message(void) { return mus.msg; }
static void mus_clock(int external) { mus.clock_ext = external ? 1 : 0; }
static void mus_arm(int armed) { mus.armed = armed ? 1 : 0; }
static void mus_hold(int hold) { mus.ahold = hold ? 1 : 0; }
static void mus_channel(uint32_t channel) { mus.channel = channel & 15u; }
static void mus_gate(uint32_t gate) { if (gate < 1u) gate = 1u; if (gate > 127u) gate = 127u; mus.agate = (uint8_t)gate; }
static void mus_octaves(uint32_t octaves) { if (octaves < 1u) octaves = 1u; if (octaves > 4u) octaves = 4u; mus.aoct = (uint8_t)octaves; }
#endif
