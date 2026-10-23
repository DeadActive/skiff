/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE */
/* What Skiff's Bluetooth build changes for its room (docs/ble/FIT.md), built as it is (FELUCCA_BLE=1) on the UI harness:
 *  - the delay line, 49152 samples (1.11 s; fx.c DLY_LEN, not a power of two): every TIME at every BPM, and an external
 *    clock's slowest tempo, stays inside it; the indices wrap by hand at its end; an impulse comes back exactly the delay
 *    later, across the wrap;
 *  - SLICE's built-in FUNK (and BREAK) left out: no sound the FM-1 starts with (power-on, a NEW project)
 *    selects them, so only a sound the user picks says NO SAMPLE;
 *  - no SMC-Mixer layout (FELUCCA_SMC 0): MODE AUTO, MIDI, FM-LINK; a stored SMC DAW reads as AUTO; the mixer is MIDI. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#if !FELUCCA_BLE
#error "build with -DFELUCCA_BLE=1"
#endif
#include "ble_ui_stub.h"
int ble_midi_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 0; }
uint32_t ble_midi_room(void) { return 0; }
#include "../firmware/src/bt_set.c"
static uint32_t smc_seen;                           /* (no SMC-Mixer layout in Skiff's Bluetooth build: FELUCCA_SMC 0) */
static void smc_in(uint32_t pkt) { (void)pkt; smc_seen++; }
#include "../firmware/src/bt_midi.c"
#include "../firmware/src/bt_fml.c"
#include "../firmware/src/bt_devs.c"
#include "../firmware/src/bt_page.c"

static int no_builtin_loop(void)                    /* no track on SLICE's BREAK or FUNK */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (eng_idx(trk[k].eng_req) == ENGI_SLICE && (slc_src_of(trk[k].p) == 0u || slc_src_of(trk[k].p) == SLC_SRC_FUNK))
            return 0;
    return 1;
}

int main(void)
{
    int bad = 0;
    uint32_t bpm, d, w, worst = 0, ok = 1, i;
    ui_power_on();
    bad += check("the Bluetooth build's delay line: 49152 samples (1.11 s at 44.1 kHz)", DLY_LEN == 49152u &&
                 sizeof dly_buf == 2u * 49152u);
    for (bpm = (uint32_t)GP[G_BPM].min; bpm <= (uint32_t)GP[G_BPM].max; bpm++)
        for (d = 0; d < 13u; d++) {
            song.g[G_BPM] = (int16_t)bpm;
            song.g[G_DTIME] = (int16_t)d;
            w = delay_samples();
            ok &= w >= 16u && w < DLY_LEN;
            worst = w > worst ? w : worst;
        }
    song.g[G_CLOCK] = 1;                              /* an external clock at 20 BPM: a beat of 132300 samples */
    midi_beat_samples = 132300u;
    for (d = 0; d < 13u; d++) {
        song.g[G_DTIME] = (int16_t)d;
        ok &= delay_samples() < DLY_LEN;
    }
    song.g[G_CLOCK] = 0;
    midi_beat_samples = 0;
    bad += check("every TIME at every BPM, and a slow external clock: the delay stays inside the line", ok &&
                 worst == DLY_LEN - 1u);
    for (ok = 1, w = DLY_LEN - 40u; w < DLY_LEN; w++)  /* the indices at the line's end */
        for (d = 16; d < DLY_LEN; d += 997u) {
            fx.dly_w = w;
            ok &= DLY_RD(d) < DLY_LEN && (DLY_RD(d) + d) % DLY_LEN == w && DLY_WR() < DLY_LEN;
            DLY_STEP();
            ok &= fx.dly_w == (w + 1u) % DLY_LEN;
        }
    bad += check("the read and write indices wrap at the line's end, the read the delay behind", ok);
    {   /* an impulse into the delay just before the wrap comes back exactly the delay later (fx_buses) */
        static int32_t cho_in[CTL], dly_in[CTL], rev_in[CTL], wet[CTL];
        uint32_t at = 0, n = 0, dl;
        memset(dly_buf, 0, sizeof dly_buf);
        memset(&fx, 0, sizeof fx);
        song.g[G_BPM] = 40;
        song.g[G_DTIME] = 0;                          /* 1/4 at 40 BPM: 66150 samples, clamped to the line */
        song.g[G_DMIX] = 127;
        song.g[G_DFDBK] = 0;
        song.g[G_CMODE] = song.g[G_CFDBK] = 0;
        dl = delay_samples();
        fx.dly_w = DLY_LEN - 5u;
        for (i = 0; i < (dl + 2u * CTL) / CTL + 1u; i++) {
            memset(dly_in, 0, sizeof dly_in);
            if (!i)
                dly_in[0] = 1 << 20;
            memset(wet, 0, sizeof wet);
            fx_buses(cho_in, dly_in, rev_in, wet, CTL);
            for (d = 0; d < CTL; d++, n++)
                if (!at && (wet[d] > 1000 || wet[d] < -1000))
                    at = n;
        }
        bad += check("an impulse across the wrap comes back the delay later", dl == DLY_LEN - 1u && at == dl &&
                     fx.dly_w < DLY_LEN);
    }
    {   /* (Skiff) no SMC-Mixer layout: MODE AUTO, MIDI, FM-LINK; a stored SMC DAW reads as AUTO; an SMC... device is MIDI */
        bt_set_t st;
        uint32_t seen[4], m;
        memset(&st, 0, sizeof st);
        st.mode = BT_MODE_SMC;
        bt_set_put(&st);
        bt_set_get(&st);
        bad += check("no SMC layout: a stored SMC DAW reads as AUTO", st.mode == BT_MODE_AUTO && !FELUCCA_SMC);
        strcpy(conn_name, "SMC-Mixer");
        conn_up = 1;
        bt_link(1);
        smc_seen = 0;
        mi_r = mi_w;
        bt_midi_in(0x09u | 0x90u << 8 | 60u << 16 | 100u << 24);
        bad += check("no SMC layout: AUTO with an SMC... device is MIDI (its notes into the input ring)",
                     !bt_mode_now() && !smc_seen && mi_w - mi_r == 1u);
        conn_up = 0;
        bt_link(0);
        mi_r = mi_w;
        ui.menu = 1;
        ui.menu_sel = MI_BT;
        frame();
        press(B_OCTUP);
        for (i = 0; i < 3u; i++) {                      /* the cursor to MODE */
            turn(EN_PRESET, 1);
            frame();
        }
        for (m = 0; m < 4u; m++) {                      /* a knob right three times, from AUTO */
            bt_set_get(&st);
            seen[m] = st.mode;
            turn(EN_K1, 1);
            frame();
        }
        bad += check("no SMC layout: MODE steps AUTO, MIDI, FM-LINK (no SMC DAW)", btp.sel == 3u &&
                     seen[0] == BT_MODE_AUTO && seen[1] == BT_MODE_MIDI && seen[2] == BT_MODE_LINK && seen[3] == BT_MODE_LINK);
        turn(EN_K1, -1); frame();
        turn(EN_K1, -1); frame();
        bt_set_get(&st);
        bad += check("no SMC layout: a knob left from FM-LINK twice: MIDI, then AUTO", st.mode == BT_MODE_AUTO);
        press(B_OCTDN);
        ui.menu = 0;
    }
    ui_power_on();
    bad += check("power-on: no track on SLICE's BREAK or FUNK", no_builtin_loop());
    project_new();
    bad += check("NEW project: no track on SLICE's BREAK or FUNK", no_builtin_loop());
    bad += check("SLICE's factory sounds on FUNK are the user's pick only (SRC 5 still named, NO SAMPLE when missing)",
                 SLC_SRC_FUNK == 5u && str_eq(N_SLC_SRC[SLC_SRC_FUNK], "FUNK"));
    printf(bad ? "ble_fit: FAILED\n" : "ble_fit: all checks passed\n");
    return bad != 0;
}
