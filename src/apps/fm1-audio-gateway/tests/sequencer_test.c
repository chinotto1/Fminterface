/* SPDX-License-Identifier: GPL-3.0-only
 * Sequencer pages: piano roll, 4-bar step entry, and a free take from the
 * keys on TEMPO, following Melodee nightly 3fe570d. */
#define main gateway_simulator_main
#include "../../../host/plat_host.c"
#undef main
#include <assert.h>
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
static unsigned notes_on(uint32_t step)
{
    unsigned n=0;
    for(uint32_t i=0;i<MUS_REC;i++) if(mus.note[0][i].used && mus.note[0][i].step==step) n++;
    return n;
}
static int roll_has(uint16_t color)
{
    color=(uint16_t)((color>>8)|(color<<8));
    for(unsigned y=38;y<181;y++) for(unsigned x=28;x<220;x++) if(fb[y*240u+x]==color) return 1;
    return 0;
}
int main(void)
{
    uint8_t step; uint16_t on; uint32_t dur;
    now_ms=10000;app_init();app_ui_init();app_loop();
    tap(B_SEQ);assert(gateway_screen==GATEWAY_SEQ && !gateway_muted && mus_ui_page==0);
    tap(B_PLAY);assert(mus.playing && !gateway_muted);
    tap(B_PLAY);assert(!mus.playing && !gateway_muted);
    tap(B_HOME);assert(gateway_screen==GATEWAY_MAIN);
    gateway_muted=1;gateway_mixer_publish();
    tap(B_PLAY);assert(!gateway_muted && mus.playing && gateway_screen==GATEWAY_MAIN);
    tap(B_PLAY);assert(!gateway_muted && !mus.playing);
    tap(B_ARP);assert(gateway_screen==GATEWAY_ARP && mus.arp==1);
    mus_ui_draw_arp();
    now_ms+=1;key_at(0,1);now_ms+=1;app_loop();
    assert(mus.arp_have);
    key_at(0,0);mus_arp_mode(0);
    tap(B_SEQ);assert(gateway_screen==GATEWAY_SEQ && !mus.arp && mus_ui_page==0);
    key_at(0,1);
    enc_acc[EN_PRESET]=1;app_input_only();
    key_at(0,0);
    assert(mus_ui_cursor==2);
    assert(mus_find(0,53,&step,&on,&dur) && step==0 && !on && dur==2u*MUS_UNIT);
    held_keys=(1u<<0)|(1u<<5);app_input_only();
    held_keys=0;app_input_only();
    assert(mus_ui_cursor==3 && notes_on(2)==2);
    mus_ui_draw();
    assert(roll_has(RGB(75,210,150)));
    enc_acc[EN_PRESET]=-1;app_input_only();
    assert(mus_ui_cursor==2);
    held_btn=1u<<B_EDIT;app_input_only();held_btn=0;app_input_only();
    assert(gateway_screen==GATEWAY_SEQ && mus_ui_page==0 && !notes_on(2));
    mus_ui_cursor=15;key_at(1,1);key_at(1,0);
    assert(mus_ui_cursor==0 && mus_step_used(0,15));
    tap(B_SEQ);assert(gateway_screen==GATEWAY_SEQ && mus_ui_page==1);
    mus_ui_draw();
    tap(B_SEQ);assert(mus_ui_page==2);
    enc_acc[EN_K1]=1;app_input_only();assert(mus.len[0]==32);
    enc_acc[EN_K1]=1;app_input_only();assert(mus.len[0]==64);
    enc_acc[EN_K1]=-1;app_input_only();assert(mus.len[0]==32);
    enc_acc[EN_K1]=-1;app_input_only();assert(mus.len[0]==16);
    mus_ui_draw();
    tap(B_SEQ);assert(mus_ui_page==3);
    mus_init();
    enc_acc[EN_K1]=-20;app_input_only();assert(mus.bpm==100 && mus_ui_page==3);
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
    mus_arp_mode(0);
    mus_length(64);
    mus_step_put(0, 4, 60, 100, MUS_UNIT);
    mus_step_put(1, 2, 67, 100, MUS_UNIT);
    assert(mus_count(0)==1 && mus_count(1)==1 && mus.len[0]==64);
    {
        uint8_t page=mus_ui_page;
        held_btn=1u<<B_SEQ;app_input_only();
        held_btn|=1u<<B_OCTDN;app_input_only();
        assert(!strcmp(mus_message(),"SEQUENCE CLEARED"));
        assert(!mus_count(0) && !mus_count(1) && mus.len[0]==64 && mus.len[1]==16 && mus.bpm==100);
        assert(mus_ui_cursor==0 && mus_ui_page==page && gateway_screen==GATEWAY_SEQ && !mus_ui_oct);
        held_btn=0;app_input_only();
        assert(mus_ui_page==page);
        held_btn=1u<<B_EDIT;app_input_only();
        now_ms+=700;app_input_only();
        held_btn=0;app_input_only();
        assert(!strcmp(mus_message(),"CLEAR UNDONE"));
        assert(mus_count(0)==1 && mus_count(1)==1 && mus.len[0]==64);
    }
    mus_ui_page=1;
    mus_length(64);
    mus_ui_cursor=2;
    enc_acc[EN_SELECT]=1;app_input_only();
    assert(mus_ui_cursor==18 && mus.sel==0 && mus_ui_page==1);
    mus_step_put(0,18,60,100,MUS_UNIT);
    held_btn=1u<<B_SEQ;app_input_only();
    held_keys=1u<<4;app_input_only();
    {
        uint8_t p=0;
        assert(mus_step_pitch(0,18,&p) && p==57 && mus_ui_cursor==18);
    }
    held_keys=0;app_input_only();
    assert(mus_ui_cursor==18);
    held_btn=0;app_input_only();
    assert(mus_ui_page==1 && gateway_screen==GATEWAY_SEQ);
    puts("PASS: piano roll, 4-bar step entry, nightly free-take, undo, arp, SEQ+OCT- clear");
    return 0;
}
