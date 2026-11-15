/* SPDX-License-Identifier: GPL-3.0-only
 * The nightly free-take cases from Melodee 3fe570d (issue 65) and the arpeggiator order. */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../music.h"
static int fails;
static void expect(int cond, const char *what)
{
    if (!cond) { fprintf(stderr, "FAIL: %s\n", what); fails++; }
}
static void phrase(uint32_t notes, uint32_t every, int hold_last)
{
    for (uint32_t i = 0; i < notes; i++) {
        mus_note(60u + i, 100, 1);
        mus_time(mus.ms + every / 2u);
        if (!hold_last || i + 1u < notes) mus_note(60u + i, 0, 0);
        mus_time(mus.ms + every - every / 2u);
    }
}
static void drain(void) { uint32_t pkt; while (mus_midi(&pkt)) ; }
static uint32_t next_on(void)
{
    uint32_t pkt;
    while (mus_midi(&pkt)) if ((pkt & 0xffu) == 9u) return (pkt >> 16) & 0xffu;
    return 0xFFFFu;
}
static void test_free_take(void)
{
    uint8_t step; uint16_t on; uint32_t dur;
    mus_init(); mus_time(0); mus_set_bpm(100);
    phrase(8, 500, 0);
    expect(mus_capture_at(4000), "4s phrase captures");
    expect(!strcmp(mus_message(), "CAPTURED 2 BARS 120 BPM"), mus_message());
    expect(mus.bpm == 120 && mus.len[0] == 32 && mus.playing, "2 bars at 120 and playing");
    expect(mus_find(0, 60, &step, &on, &dur) && step == 0 && !on && dur == 2u * MUS_UNIT, "first note on the 1, as long as played");
    expect(mus_find(0, 64, &step, &on, &dur) && step == 16 && !on, "middle note on bar 2");
    expect(mus_find(0, 67, &step, &on, &dur) && step == 28, "last note on its step");
    expect(mus_undo() && mus.bpm == 100 && mus.len[0] == 16 && !mus_find(0, 60, 0, 0, 0) && !mus.playing, "undo restores the empty pattern and tempo");

    mus_init(); mus_time(0); mus_set_bpm(118);
    phrase(8, 500, 1);
    expect(mus_capture_at(4000), "within 3% captures");
    expect(!strcmp(mus_message(), "CAPTURED 2 BARS 118 BPM") && mus.bpm == 118 && mus.len[0] == 32, "tempo within 3% stays");
    expect(mus_find(0, 64, &step, &on, &dur) && step == 16, "grid stays put when tempo stays");
    expect(mus_find(0, 67, &step, &on, &dur) && dur == 4u * MUS_UNIT, "a note still held reaches the press");

    mus_init(); mus_time(0); mus_set_bpm(90);
    mus_seed(3, 0, 36);
    phrase(8, 500, 0);
    expect(mus_capture_at(4000), "other track captures");
    expect(!strcmp(mus_message(), "CAPTURED 2 BARS 90 BPM") && mus.bpm == 90 && mus.len[0] == 32, "another track keeps the tempo");
    expect(mus_find(0, 61, &step, &on, &dur) && step == 3, "notes keep their times at the current tempo");

    mus_init(); mus_time(0); mus_set_bpm(100);
    phrase(2, 500, 0);
    mus_time(5000);
    expect(mus_capture_at(5000) && !strcmp(mus_message(), "CAPTURED 1 BAR 100 BPM") && mus.bpm == 100 && mus.len[0] == 16,
           "a late press keeps the tempo");

    mus_init(); mus_time(0); mus_set_bpm(100);
    phrase(1, 500, 0);
    expect(mus_capture_at(500) && !strcmp(mus_message(), "CAPTURED 1 BAR 100 BPM") && mus.bpm == 100, "one note keeps the tempo");

    mus_init(); mus_time(0); mus_set_bpm(100);
    phrase(8, 500, 0);
    mus_time(mus.ms + MUS_REST_MS + 500u);
    phrase(8, 500, 0);
    expect(mus_capture_at(mus.ms) && !strcmp(mus_message(), "CAPTURED 2 BARS 120 BPM"), "a long rest starts the phrase again");
    expect(mus_find(0, 60, &step, &on, &dur) && step == 0 && mus_find(0, 67, &step, &on, &dur) && step == 28, "only the new phrase is kept");

    mus_init(); mus_time(0); mus_set_bpm(100);
    phrase(8, 500, 0);
    mus_time(mus.ms + MUS_REST_MS + 500u);
    expect(!mus_capture_at(mus.ms) && !strcmp(mus_message(), "NOTHING TO CAPTURE") && mus.bpm == 100 && !mus.playing, "a forgotten phrase captures nothing");

    mus_init(); mus_time(0); mus_set_bpm(100);
    phrase(8, 500, 0);
    mus_seed(0, 3, 40);
    expect(!mus_capture_at(4000) && !strcmp(mus_message(), "NOTHING TO CAPTURE") && !mus.playing && mus.free, "a step entered since captures nothing");

    mus_init(); mus_time(0);
    mus_seed(0, 3, 40);
    mus_note(60, 100, 1); mus_note(60, 0, 0);
    expect(!mus.free, "a pattern that already has notes does not start a free take");

    mus_init(); mus_time(0); mus_clock(1);
    mus_note(60, 100, 1); mus_note(60, 0, 0);
    expect(!mus.free, "an external clock does not take a tempo from the notes");

    mus_init(); mus_time(0); mus_arm(1);
    mus_note(60, 100, 1); mus_note(60, 0, 0);
    expect(!mus.free && !mus_capture_at(mus.ms), "an armed track does not free-take");
}
static void test_playing_capture(void)
{
    uint8_t step; uint16_t on; uint32_t dur;
    mus_init(); mus_time(0); mus_set_bpm(120);
    mus_play(1);
    mus_note(60, 100, 1);
    mus_time(125); mus_note(60, 0, 0);
    mus_note(64, 100, 1);
    mus_time(250); mus_note(64, 0, 0);
    mus_time(2000);
    expect(mus_capture_at(2000), "playing capture");
    expect(!strcmp(mus_message(), "CAPTURED 1 BAR"), mus_message());
    expect(mus_find(0, 60, &step, &on, &dur) && step == 0 && dur == MUS_UNIT, "downbeat note kept for one step");
    expect(mus_find(0, 64, &step, &on, &dur) && step == 1, "the next step is kept");
}
static void test_arp(void)
{
    mus_init(); mus_time(0); mus_set_bpm(120); mus_arp_mode(1);
    mus_note(64, 100, 1); mus_note(60, 100, 1); mus_note(67, 100, 1);
    mus_time(1);
    expect(next_on() == 60, "arp up starts on the lowest note");
    mus_time(mus.ms + 125);
    expect(next_on() == 64, "arp up continues upward");
    mus_time(mus.ms + 125);
    expect(next_on() == 67, "arp up reaches the top");

    mus_init(); mus_time(0); mus_set_bpm(120); mus_arp_mode(2);
    mus_note(60, 100, 1); mus_note(64, 100, 1); mus_note(67, 100, 1);
    mus_time(1);
    expect(next_on() == 67, "arp down starts on the highest note");

    mus_init(); mus_time(0); mus_set_bpm(120); mus_arp_mode(3);
    mus_note(60, 100, 1); mus_note(64, 100, 1); mus_note(67, 100, 1);
    mus_time(1); expect(next_on() == 60, "up-down starts at the bottom");
    mus_time(mus.ms + 125); expect(next_on() == 64, "up-down rises");
    mus_time(mus.ms + 125); expect(next_on() == 67, "up-down reaches the top");
    mus_time(mus.ms + 125); expect(next_on() == 64, "up-down turns back");

    mus_init(); mus_time(0); mus_set_bpm(120); mus_arp_mode(6);
    mus_note(60, 100, 1); mus_note(67, 100, 1);
    mus_time(1);
    expect(next_on() == 67, "repeat plays the last note");
    mus_time(mus.ms + 125);
    expect(next_on() == 67, "repeat stays on the last note");

    mus_init(); mus_time(0); mus_set_bpm(120); mus_arp_mode(1); mus_octaves(2);
    mus_note(60, 100, 1);
    mus_time(1); expect(next_on() == 60, "octave span starts on the held note");
    mus_time(mus.ms + 125); expect(next_on() == 72, "octave span continues an octave up");
}
static void test_playback(void)
{
    mus_init(); mus_time(0); mus_set_bpm(120);
    phrase(8, 500, 0);
    expect(mus_capture_at(4000), "playback source");
    drain();
    mus_time(mus.ms + 1);
    expect(next_on() == 60, "the captured loop plays from the top");
}
static void test_clock(void)
{
    mus_init();
    mus_poll(5000);
    expect(mus.ms == 5000 && !mus.pos, "the first poll joins the clock without moving");
    mus_set_bpm(120); mus_play(1); mus_seed(0, 0, 72);
    mus_poll(5001);
    expect(next_on() == 72, "a seeded step plays on the next millisecond");
}
int main(void)
{
    test_free_take();
    test_playing_capture();
    test_arp();
    test_playback();
    test_clock();
    if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
    puts("PASS: nightly free-take BPM capture, playing capture, arp order and loop playback");
    return 0;
}
