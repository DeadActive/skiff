/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* (Skiff, #21) a user slot's sounds' LEVEL, REV and DLY (eng_sample.c usr_mix), through hostsim.c (run_tests.sh):
 *   build/host/smpmix_test
 * Slots are built here (two or three sounds of a crude ADPCM wave, one key each) in a RAM image of the flash slots
 * (SMP_USER_XIP), read by smp_user_scan as at boot.
 * 1. a slot of before (no record) plays every sound as uploaded: LEVEL 127, REV 0, DLY 0;
 * 2. the store (slice_store.c): written once stopped and off the SLICES page, only when it changed ("MIX SAVED"), read back
 *    at a scan; kept for its header only (another sample, or the same under other keys: as uploaded again; the same
 *    upload again: back); a save of SLICE's record keeps it; a torn write or junk ignored; a long sample: "TOO LONG TO
 *    SAVE", the values stay until the power goes;
 * 3. LEVEL: a sound at 64 plays at a quarter (square law, as DRUM's lanes), at 127 as before; a built-in set as before;
 *    values out of range (a crafted record) play at full level;
 * 4. REV / DLY: a sound's own sends reach the REVERB and DELAY sums as the track's would by as much (with the track's,
 *    127 at most), at the track's LEVEL; none from a sound without them, none from a built-in set's, none while the
 *    FX layer mutes the track; the dry mix as before. */
#include <stdarg.h>
#include <stdint.h>
static uint32_t host_slots[3u * 0x14000u / 4u];          /* USR1..3, as the flash at 0xA0000 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

/* the store's hooks over host_slots, the UI's stubs (as slice_test's) */
static uint8_t flash_ok = 1, host_busy, host_page, host_torn;
static char host_msg[32];
static int transport_busy(void) { return host_busy; }
static int slice_page_on(void) { return host_page; }
static int smx_page_on(void) { return 0; }               /* ((#21) the SAMPLES page: as SLICES', above) */
static void ui_message(const char *s) { snprintf(host_msg, sizeof host_msg, "%s", s); }
static uint32_t host_erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { (void)off; (void)dst; (void)n; return -1; }
static int st_erase(uint32_t off)
{
    if (off < SMP_USER_BASE || off + 0x1000u > SMP_USER_BASE + sizeof host_slots || (off & 0xFFFu))
        return -8;
    memset((uint8_t *)host_slots + (off - SMP_USER_BASE), 0xFF, 0x1000u);
    host_erases++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (off < SMP_USER_BASE || off + n > SMP_USER_BASE + sizeof host_slots)
        return -8;
    if (host_torn)
        return -1;                                       /* power gone after the erase */
    for (i = 0; i < n; i++)                              /* NOR: bits only go 1 -> 0 */
        ((uint8_t *)host_slots)[off - SMP_USER_BASE + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/slice_store.c"

static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("smpmix: %-62s %s", what, ok ? "ok" : "FAIL");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        printf("  (");
        vprintf(fmt, ap);
        printf(")");
        va_end(ap);
    }
    printf("\n");
    fails += !ok;
}

/* slot k: nz sounds of zb ADPCM bytes each (a wave going 4 samples up, 4 down), sound i on key 60 + i (lo = hi), its
 * name, salt changing the first data byte (another sample); an erased slot under it */
static void slot_put(uint32_t k, uint32_t nz, uint32_t zb, uint8_t salt)
{
    uint8_t *b = (uint8_t *)host_slots + k * SMP_USER_SIZE;
    smp_user_hdr_t h;
    uint32_t i;
    memset(b, 0xFF, SMP_USER_SIZE);
    memset(&h, 0, sizeof h);
    h.magic = SMP_USER_MAGIC;
    h.version = 1;
    h.nz = (uint8_t)nz;
    memcpy(h.name, "MIX", 3);
    h.data_len = nz * zb;
    for (i = 0; i < h.data_len; i++)
        b[SMP_USER_DATA + i] = (i & 2u) ? 0xFFu : 0x77u;
    b[SMP_USER_DATA] ^= salt;
    for (i = 0; i < nz; i++) {
        smp_zone_t *z = &h.zone[i];
        z->off = i * zb;
        z->n = 2u * zb;
        z->le = z->n - 1u;
        z->rate = 65536u;
        z->root16 = (int16_t)((60 + (int32_t)i) * 16);
        z->lo = z->hi = (uint8_t)(60u + i);
    }
    h.crc = st_crc32(b + SMP_USER_DATA, h.data_len);
    memcpy(b, &h, sizeof h);
}
static void mix_set(uint32_t k, uint32_t z, int lv, int rv, int dl)
{
    usr_mix[k][SMX_LVL][z] = (int16_t)lv;
    usr_mix[k][SMX_REV][z] = (int16_t)rv;
    usr_mix[k][SMX_DLY][z] = (int16_t)dl;
}
static int mix_is(uint32_t k, uint32_t z, int lv, int rv, int dl)
{
    return usr_mix[k][SMX_LVL][z] == lv && usr_mix[k][SMX_REV][z] == rv && usr_mix[k][SMX_DLY][z] == dl;
}
static int all_neutral(uint32_t k)
{
    uint32_t z;
    int ok = 1;
    for (z = 0; z < 16u; z++)
        ok &= mix_is(k, z, 127, 0, 0);
    return ok;
}
static int str_is(const char *a, const char *b) { return !strcmp(a, b); }
/* the poll as the main loop runs it, stopped and off the page */
static void store(void)
{
    host_busy = host_page = 0;
    host_msg[0] = 0;
    slc_store_poll();
}

/* 3, 4: track 1 on SAMPLE, SET set (USR1 = SMP_NSETS), no DRIVE / INSERT, its sends rev and dly, LEVEL lvl, ADSR 0 / 127
 * / 127 / 10, plays note at velocity 100 for nb blocks: the dry mix's peak (left, before the buses' return), the REVERB
 * and DELAY sums' energy */
typedef struct {
    uint32_t peak;
    double er, ed;
} run_t;
static run_t run(int16_t set, uint32_t note, int16_t rev, int16_t dly, int16_t lvl, uint32_t nb)
{
    track_t *t = &trk[0];
    run_t r = {0, 0, 0};
    uint32_t i, k, j;
    for (k = 0; k < NTRK; k++)
        for (j = 0; j < NVOICE; j++)
            trk[k].v[j].active = trk[k].v[j].gate = 0;
    host_tracks_init();
    song.g[G_RTYPE] = 0;                                 /* (ROOM: no stereo side in the dry mix) */
    host_preset(t, ENGI_SAMPLE, 0);
    t->p[P_E0] = set;
    t->p[P_E2] = 0;                                      /* (BITS, CUTOFF, DRIVE: clean) */
    t->p[P_E4] = 127;
    t->p[P_E6] = 0;
    t->p[P_DIST] = t->p[P_CHOR] = 0;
    t->p[P_REV] = rev;
    t->p[P_DLY] = dly;
    t->p[P_LEVEL] = lvl;
    t->p[P_ATK] = 0;
    t->p[P_DEC] = 127;
    t->p[P_SUS] = 127;
    t->p[P_REL] = 10;
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);                               /* (the engine in) */
    }
    trk_note_on(t, note, 100);
    for (k = 0; k < nb; k++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            uint32_t a = (uint32_t)abs(mix_l[i]);
            r.peak = a > r.peak ? a : r.peak;
            r.er += (double)send_r[i] * send_r[i];
            r.ed += (double)send_d[i] * send_d[i];
        }
    }
    trk_note_off(t, note);
    return r;
}
static int close_to(double a, double b, double tol) { return b > 0 ? fabs(a / b - 1.0) <= tol : a == 0; }

int main(void)
{
    const uint8_t *tail = (const uint8_t *)host_slots + SLC_REC_OFF;
    int ok;
    uint32_t e0;
    slc_store_boot();                                    /* (persist_boot: the scans read the records) */

    /* 1: a slot of before */
    slot_put(0, 3, 512, 0);
    mix_set(0, 1, 9, 9, 9);
    smp_user_scan(0);
    check("a slot of before plays every sound as uploaded (127, 0, 0)", usr_nz[0] == 3 && all_neutral(0), 0);

    /* 2: the store */
    mix_set(0, 0, 64, 40, 0);
    mix_set(0, 2, 100, 0, 90);
    smx_save |= 1u;
    host_busy = 1;
    e0 = host_erases;
    slc_store_poll();
    ok = host_erases == e0 && smx_save == 1u;            /* playing: waits */
    host_busy = 0;
    host_page = 1;
    slc_store_poll();
    ok &= host_erases == e0 && smx_save == 1u;           /* the SLICES page open: waits */
    store();
    ok &= host_erases == e0 + 1u && !smx_save && str_is(host_msg, "MIX SAVED");
    smx_save |= 1u;
    store();
    ok &= host_erases == e0 + 1u && !host_msg[0];        /* the same again: no erase, nothing said */
    check("store: written once stopped and off the page, only when it changed", ok, 0);

    smp_user_scan(0);
    check("store: a scan (boot) puts the values back",
          mix_is(0, 0, 64, 40, 0) && mix_is(0, 1, 127, 0, 0) && mix_is(0, 2, 100, 0, 90), 0);

    slc_man_save |= 1u;                                  /* SLICE's record saved again (MAN slices: none) */
    mix_set(0, 1, 30, 0, 0);
    store();
    smp_user_scan(0);
    ok = str_is(host_msg, "SLICES SAVED") && mix_is(0, 0, 64, 40, 0) && mix_is(0, 1, 30, 0, 0);
    check("store: a save of SLICE's record keeps (and writes) the mix", ok, 0);

    {
        uint8_t keep[0x1000];
        memcpy(keep, tail, sizeof keep);                 /* the last sector as the store left it (an upload leaves it) */
        slot_put(0, 3, 512, 0x10);                       /* another sample */
        memcpy((uint8_t *)host_slots + SLC_REC_OFF, keep, sizeof keep);
        smp_user_scan(0);
        ok = all_neutral(0);
        slot_put(0, 3, 512, 0);                          /* the first sample again, the same keys */
        memcpy((uint8_t *)host_slots + SLC_REC_OFF, keep, sizeof keep);
        smp_user_scan(0);
        ok &= mix_is(0, 0, 64, 40, 0) && mix_is(0, 1, 30, 0, 0);
        slot_put(0, 3, 512, 0);                          /* .. under other keys: as uploaded */
        ((smp_user_hdr_t *)host_slots)->zone[0].hi = 59;
        ((smp_user_hdr_t *)host_slots)->zone[0].lo = 0;
        memcpy((uint8_t *)host_slots + SLC_REC_OFF, keep, sizeof keep);
        smp_user_scan(0);
        ok &= usr_nz[0] == 3 && all_neutral(0);
        check("store: kept for its header only (another sample, other keys: as uploaded; the same again: back)", ok, 0);

        slot_put(0, 3, 512, 0);
        memcpy((uint8_t *)host_slots + SLC_REC_OFF, keep, sizeof keep);
        ((uint8_t *)host_slots)[SMX_REC_OFF + 5] ^= 1u;   /* a bit gone */
        smp_user_scan(0);
        ok = all_neutral(0);
        memset((uint8_t *)host_slots + SMX_REC_OFF, 0, sizeof(smx_rec_t));   /* zeros */
        smp_user_scan(0);
        ok &= all_neutral(0);
        check("store: a damaged or zeroed record is ignored", ok, 0);
    }

    slot_put(0, 3, 512, 0);
    smp_user_scan(0);
    mix_set(0, 0, 50, 0, 0);
    smx_save |= 1u;
    host_torn = 1;
    store();
    host_torn = 0;
    ok = str_is(host_msg, "SAVE ERROR");
    smp_user_scan(0);
    ok &= all_neutral(0) && usr_nz[0] == 3;
    check("store: a write cut short loses the values, never the sample (SAVE ERROR)", ok, 0);

    slot_put(0, 2, (SMP_USER_SIZE - SMP_USER_DATA) / 2u, 0);   /* the sample fills the slot */
    smp_user_scan(0);
    mix_set(0, 1, 40, 0, 0);
    smx_save |= 1u;
    e0 = host_erases;
    store();
    ok = str_is(host_msg, "TOO LONG TO SAVE") && host_erases == e0 && mix_is(0, 1, 40, 0, 0) && !smx_save;
    smp_user_scan(0);
    ok &= all_neutral(0);
    check("store: a sample into the last sector: TOO LONG TO SAVE, kept in RAM only", ok, 0);

    /* 3: LEVEL */
    slot_put(0, 3, 4096, 0);
    smp_user_scan(0);
    {
        run_t a, b, c, d, x;
        a = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 40);
        mix_set(0, 1, 64, 0, 0);
        b = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 40);
        c = run((int16_t)SMP_NSETS, 60, 0, 0, 112, 40);   /* another sound: its own (127) */
        mix_set(0, 1, -5, 0, 0);                         /* crafted: out of range */
        d = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 40);
        mix_set(0, 1, 127, 0, 0);
        x = run(0, 60, 0, 0, 112, 40);                   /* a built-in set */
        ok = a.peak > 4000u && close_to(b.peak, a.peak * 8256.0 / 32508.0, 0.03) && c.peak > 4000u && d.peak == a.peak;
        check("LEVEL: 64 a quarter (square law), 127 as before, out of range full", ok,
              "127: %u, 64: %u (%.3f), out of range %u", a.peak, b.peak, (double)b.peak / a.peak, d.peak);
        mix_set(0, 1, 64, 0, 0);
        {
            run_t y = run(0, 60, 0, 0, 112, 40);
            check("LEVEL: a built-in set's sounds are as before", y.peak == x.peak && x.peak > 1000u, "%u %u", x.peak, y.peak);
        }
        mix_set(0, 1, 127, 0, 0);
    }

    /* 4: REV / DLY */
    {
        run_t t0, t64, z64, zd, both, cap, lvl, lv0, none, muted;
        mix_set(0, 1, 127, 0, 0);
        none = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 60);
        t64 = run((int16_t)SMP_NSETS, 61, 64, 0, 112, 60);   /* the track's REVERB 64 */
        mix_set(0, 1, 127, 64, 0);
        z64 = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 60);    /* the sound's REV 64 */
        ok = none.er == 0 && none.ed == 0 && t64.er > 0 && close_to(z64.er, t64.er, 0.02) && z64.ed == 0 &&
             z64.peak == none.peak;
        check("REV: the sound's 64 sends as the track's 64 would, the dry as before", ok, "track %.4g, sound %.4g",
              t64.er, z64.er);
        mix_set(0, 1, 127, 0, 50);
        zd = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 60);
        mix_set(0, 1, 127, 0, 0);                             /* (the references: the track's sends alone) */
        t0 = run((int16_t)SMP_NSETS, 61, 0, 50, 112, 60);
        check("DLY: the sound's 50 sends as the track's 50 would", close_to(zd.ed, t0.ed, 0.02) && zd.er == 0 && t0.ed > 0,
              "track %.4g, sound %.4g", t0.ed, zd.ed);
        mix_set(0, 1, 127, 64, 0);
        both = run((int16_t)SMP_NSETS, 61, 30, 0, 112, 60);   /* 30 + 64 */
        mix_set(0, 1, 127, 120, 0);
        cap = run((int16_t)SMP_NSETS, 61, 100, 0, 112, 60);   /* 100 + 120: 127 at most */
        mix_set(0, 1, 127, 0, 0);
        t0 = run((int16_t)SMP_NSETS, 61, 94, 0, 112, 60);
        t64 = run((int16_t)SMP_NSETS, 61, 127, 0, 112, 60);
        check("REV: added to the track's (30 + 64 as 94), 127 at most in all", close_to(both.er, t0.er, 0.02) &&
              close_to(cap.er, t64.er, 0.02), "%.4g %.4g, %.4g %.4g", both.er, t0.er, cap.er, t64.er);
        mix_set(0, 1, 127, 64, 0);
        lvl = run((int16_t)SMP_NSETS, 61, 0, 0, 100, 60);
        lv0 = run((int16_t)SMP_NSETS, 61, 0, 0, 0, 60);
        mix_set(0, 1, 127, 0, 0);
        t0 = run((int16_t)SMP_NSETS, 61, 64, 0, 100, 60);
        check("REV: at the track's LEVEL (LEVEL 0: nothing)", close_to(lvl.er, t0.er, 0.02) && lv0.er == 0, 0);
        mix_set(0, 1, 127, 64, 0);
        none = run((int16_t)SMP_NSETS, 60, 0, 0, 112, 60);    /* another sound of the slot: none of its own */
        t0 = run(0, 60, 0, 0, 112, 60);                       /* a built-in set */
        check("REV: none from a sound without it, none from a built-in set", none.er == 0 && t0.er == 0 && none.peak > 0, 0);
        perf_held = 1u << PF_M1;                              /* the FX layer mutes track 1 */
        muted = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 60);
        perf_held = 0;
        {
            int32_t o[2 * CTL];
            uint32_t k;
            for (k = 0; k < 20u; k++)
                mix_block(o, CTL);                            /* (its ramp back) */
        }
        check("REV: none while the FX layer mutes the track (after its ramp)", muted.er < z64.er * 0.05, "%.4g of %.4g",
              muted.er, z64.er);
        mix_set(0, 1, 127, 0, 0);
    }

    printf("%s\n", fails ? "SMPMIX TEST FAILED" : "smpmix test passed");
    return fails != 0;
}
