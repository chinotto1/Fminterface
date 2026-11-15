/* SPDX-License-Identifier: GPL-3.0-only
 * Sequencer page: transport does not mute, and a free take from the keys
 * follows Melodee nightly 3fe570d. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
#include <string.h>
static void tap(unsigned b){held_btn=1u<<b;app_input_only();held_btn=0;app_input_only();}
static void key_at(unsigned key, int down){held_keys=down?1u<<key:0;app_input_only();}
static void phrase(void)
{
    for(unsigned i=0;i<8u;i++){
        key_at(0,1);
        now_ms+=250;key_at(0,0);
        now_ms+=250;
    }
}
int main(void)
{
    uint8_t step; uint16_t on; uint32_t dur;
    now_ms=10000;app_init();app_ui_init();app_loop();
    tap(B_SEL);assert(gateway_screen==GATEWAY_SEQ && !gateway_muted);
    tap(B_PLAY);assert(mus.playing && !gateway_muted);
    tap(B_PLAY);assert(!mus.playing && !gateway_muted);
    tap(B_HOME);assert(gateway_screen==GATEWAY_MAIN);
    tap(B_PLAY);assert(gateway_muted && gateway_screen==GATEWAY_MAIN);
    tap(B_PLAY);assert(!gateway_muted);
    tap(B_SEL);assert(gateway_screen==GATEWAY_SEQ);
    enc_acc[EN_K1]=-20;app_input_only();assert(mus.bpm==100);
    phrase();
    held_btn=1u<<B_REC;app_input_only();
    now_ms+=700;app_input_only();
    held_btn=0;app_input_only();
    assert(!strcmp(mus_message(),"CAPTURED 2 BARS 120 BPM"));
    assert(mus.bpm==120 && mus.len[0]==32 && mus.playing && !gateway_muted);
    assert(mus_count(0)==8u && mus_find(0,53,&step,&on,&dur) && step==0 && !on && dur==2u*MUS_UNIT);
    held_btn=1u<<B_EDIT;app_input_only();
    now_ms+=700;app_input_only();
    held_btn=0;app_input_only();
    assert(gateway_screen==GATEWAY_SEQ && !mus.playing && mus.bpm==100 && mus.len[0]==16 && !mus_count(0));
    enc_acc[EN_ALGO]=1;app_input_only();assert(mus.arp==1);
    puts("PASS: sequencer page transport, nightly free-take from the keys, undo, arp mode");
    return 0;
}
