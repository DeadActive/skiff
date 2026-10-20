/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Firmware save flows against NOR flash: deferred settings, retries, and failed-save rollback. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static uint32_t irq_start_race, irq_races;
static void fm1_irq_off(void)
{
    if (irq_start_race) {                         /* audio serviced PLAY immediately before main masks it */
        irq_start_race = 0;
        events_block(CTL);
        irq_races++;
    }
}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }   /* (MENU > SCREEN OFF: lcd.c) */
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define FELUCCA_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
static void panel_setup(void) {}

#if BOARD_SMK37                                 /* (-DBOARD_SMK37=1: the SMK-37 Pro's layout, storage.c, fm1_flash_map.h) */
#define T_AS 0xA9000u                           /* the autosave's pair */
#define T_FM6_A 0xAB000u                        /* the FM6 patches' pair (OBJ_FM6BANK / OBJ_UPFM6) */
#define T_FM6_B 0xAC000u
#define T_POOL(off) ((off) >= FL_BANK_LO && (off) < FL_BANK_HI)
#else
#define T_AS 0xE5000u
#define T_FM6_A 0x9F000u
#define T_FM6_B 0xFE000u
#define T_POOL(off) (((off) >= 0xE7000u && (off) < 0xE9000u) || ((off) >= 0xEA000u && (off) < 0xFC000u))
#endif
static uint8_t nor[0x100000], flash_ok = 1;
static int erase_error, fail_after = -1;
static uint32_t erases, as_erases, pool_erases;  /* (as_erases: the autosave's two sectors, 0xE5000 / 0xE6000; pool_erases:
                                                  * the pattern pool's twenty, bank.c bk_sector) */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    erases++;
    as_erases += off >= T_AS && off < T_AS + 0x2000u;
    pool_erases += T_POOL(off);
    if (erase_error) return -8;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0) return -9;
    if (fail_after > 0) fail_after--;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static void fm1_wdt_feed(void) {}
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "../firmware/src/bank.c"                 /* (Skiff phase 5: the patterns a project names; kept before saves) */

enum { ED_UI_STATE = 34, ED_UI_SET, ED_UI_PALETTES, ED_FAV_GET, ED_FAV_SET, ED_COLORS };   /* ((Skiff #27) 39) */
static uint8_t ed_rep[64];                        /* the reply's bytes (MENU_SET) */
static uint32_t ed_repn;
static void ed_b(uint32_t n) { if (ed_repn < sizeof ed_rep) ed_rep[ed_repn++] = (uint8_t)(n & 127u); }
static void ed_v(int32_t n) { ed_b((uint32_t)(n + 8192)); ed_b((uint32_t)(n + 8192) >> 7); }
static int32_t ed_rv(const uint8_t *a) { return (int32_t)(a[0] | (uint32_t)a[1] << 7) - 8192; }
static void ed_str(const char *s, uint32_t n) { (void)s; (void)n; }
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)   /* (editor.c's, (Skiff #27) COLORS) */
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j, k;
        na--;
        k = na > 7u ? 7u : na;
        if (!k || n + k > max || (m >> k))
            return 0;
        for (j = 0; j < k; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return na ? 0u : n;
}
static void ed_bk_pack(const uint8_t *p, uint32_t n)   /* (editor_backup.c's) */
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, mask = 0, i;
        for (i = 0; i < k; i++) mask |= (uint32_t)(p[i] >> 7) << i;
        ed_b(mask);
        for (i = 0; i < k; i++) ed_b(p[i]);
        p += k; n -= k;
    }
}
#include "../firmware/src/editor_preferences.c"
#include "../firmware/src/editor_menu.c"

static int check(const char *what, int ok)
{
    printf("persist: %-66s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(up_bank, 0, sizeof up_bank);
    memset(&persist_saved, 0, sizeof persist_saved);
    memset(&settings, 0, sizeof settings);
    memset(&ui, 0, sizeof ui);
    memset(&favorites, 0, sizeof favorites);
    panel = PANEL_DEFAULT;
    host_tracks_init();
    fm1_ms = 0;
    transport_req = 0;
    persist_pending = 0;
    persist_retry_ms = 0;
    flash_ok = 1;
    erase_error = 0;
    fail_after = -1;
    up_gen = erases = 0;
    irq_start_race = irq_races = 0;
    bank_boot();                                  /* (Skiff phase 5: the pattern pool, empty; the lists empty) */
    lists_clear();
    motion_lost = 0;
}

/* ---- 1.0.3: the retired FM6 bank's patches move into the user presets on the first boot (up_fm6.c) ---- */
static uint8_t mig_pre[0x100000];
static fm6_bank_t mig_bank;

static void mig_rec(up_bank_t *b, uint32_t i, uint32_t engine, int16_t slot, char tag)
{
    up_rec_t *r = &b->r[i];
    uint32_t k;
    memset(r, 0, sizeof *r);
    r->used = UP_USED; r->ver = UP_VER; r->engine = (uint8_t)engine; r->np = P_COUNT;
    r->name[0] = tag;
    for (k = 0; k < P_COUNT; k++) up_set_value(r, k, param_desc_of(engine, k)->def);
    up_set_value(r, P_E7, slot);
    up_set_value(r, P_E0, (int16_t)(tag & 7));          /* (each record another sound) */
}

/* a 1.0.2 flash: user presets on B3 (slot 1), F2 (2), B5 empty (3), DRUM (4), B1 (18); the bank written twice */
static void mig_setup(void)
{
    static up_bank_t b[2];
    memset(nor, 0xFF, sizeof nor);
    memset(b, 0, sizeof b);
    for (uint32_t i = 0; i < 2u; i++) { b[i].magic = UP_BANK_MAGIC; b[i].rsize = sizeof(up_rec_t); b[i].nslot = UP_PER_BANK; }
    mig_rec(&b[0], 0, ENGI_FM6, FM6_NFACTORY + 2, 'A');
    mig_rec(&b[0], 1, ENGI_FM6, 1, 'B');
    mig_rec(&b[0], 2, ENGI_FM6, FM6_NFACTORY + 4, 'C');
    mig_rec(&b[0], 3, ENGI_DRUM, FM6_NFACTORY + 2, 'D');
    mig_rec(&b[1], 1, ENGI_FM6, FM6_NFACTORY + 0, 'E');
    st_save(OBJ_UPRESET0, &b[0], sizeof b[0]);
    st_save(OBJ_UPRESET0 + 1, &b[1], sizeof b[1]);
    memset(&mig_bank, 0, sizeof mig_bank);
    mig_bank.magic = FM6_BANK_MAGIC; mig_bank.ver = 1; mig_bank.nslot = FM6_BANK_N; mig_bank.used = 1u | 1u << 2;
    memcpy(mig_bank.v[2], FM6_FACTORY[4], FM6_PACKED); memcpy(mig_bank.v[2] + 118, "OLD BANK 1", 10);
    st_save(OBJ_FM6BANK, &mig_bank, sizeof mig_bank);   /* A: an older bank */
    memcpy(mig_bank.v[0], FM6_FACTORY[6], FM6_PACKED); memcpy(mig_bank.v[0] + 118, "NEW B1    ", 10);
    memcpy(mig_bank.v[2] + 118, "NEW B3    ", 10);
    st_save(OBJ_FM6BANK, &mig_bank, sizeof mig_bank);   /* B: the newest */
}

static void mig_boot(void)                               /* a power-on: RAM lost, the stores from flash */
{
    memset(up_bank, 0, sizeof up_bank);
    memset(&upf, 0, sizeof upf);
    up_boot();
}

/* the presets play what 1.0.2 played: slot 1 the newest B3, 18 B1, 2 F2, 3 init (an empty B5), DRUM none */
static int mig_ok(void)
{
    uint8_t pk[FM6_PACKED];
    return !upf_get(0, pk) && !memcmp(pk + 118, "NEW B3    ", 10) && !upf_get(17, pk) &&
           !memcmp(pk + 118, "NEW B1    ", 10) && upf_get(1, pk) && upf_get(2, pk) && upf_get(3, pk);
}

static int mig_test(void)
{
    int bad = 0, ok = 1;
    uint32_t cut, before, cuts = 0;
    uint8_t pk[FM6_PACKED];
    upf_t got;
    reset();
    mig_setup();
    memcpy(mig_pre, nor, sizeof nor);
    mig_boot();
    bad += check("FM6 bank -> user presets: B slots get the bank's newest patches", mig_ok());
    bad += check("  .. written into A (the bank's older copy); the bank's newest copy (B) still whole",
                 st_load(OBJ_UPFM6, &got, sizeof got) == (int)sizeof got && !memcmp(&got, &upf, sizeof got) &&
                     ((st_hdr_t *)(nor + T_FM6_A))->type == OBJ_UPFM6 && st_load(OBJ_FM6BANK, &mig_bank, sizeof mig_bank) > 0 &&
                     !memcmp(mig_bank.v[0] + 118, "NEW B1", 6));
    up_load(0);
    bad += check("  .. UP_LOAD 1 plays the bank's B3, SLOT OWN", !memcmp(fm6_patch[song.sel] + FP_NAME, "NEW B3    ", 10) &&
                 TSEL->p[P_E7] == FM6_OWN);
    up_load(1);
    bad += check("  .. UP_LOAD 2 (F2) plays F2, SLOT F2", !memcmp(fm6_patch[song.sel] + FP_NAME, FM6_FACTORY[1] + 118, 10) &&
                 TSEL->p[P_E7] == 1);
    up_load(2);
    bad += check("  .. UP_LOAD 3 (an empty B5) plays the init voice, as before",
                 !memcmp(fm6_patch[song.sel] + FP_NAME, "INIT VOICE", 10) && TSEL->p[P_E7] == FM6_OWN);
    before = erases;
    mig_boot();
    bad += check("  .. the next boot: done (no write), the same patches", mig_ok() && erases == before);
    /* a power cut at every write step of the migration: the next boot ends with the same result */
    for (cut = 0; cut < 64u && ok; cut++) {
        memcpy(nor, mig_pre, sizeof nor);
        fail_after = (int)cut;
        mig_boot();                                      /* (writes stop at the cut: the rest fails) */
        if (fail_after > 0) break;                       /* (this boot finished before the cut) */
        fail_after = -1;
        cuts++;
        mig_boot();
        ok &= mig_ok() && st_load(OBJ_UPFM6, &got, sizeof got) == (int)sizeof got;
    }
    memcpy(nor, mig_pre, sizeof nor);
    erase_error = 1;
    mig_boot();                                          /* a cut before the erase */
    erase_error = 0;
    mig_boot();
    ok &= mig_ok();
    bad += check("a power cut at any step of the migration loses nothing (the next boot redoes it)", ok && cuts >= 10u);
    /* after it: a preset save writes the other sector (the bank's last copy); a cut there keeps the first copy */
    memcpy(nor, mig_pre, sizeof nor);
    fail_after = -1;
    mig_boot();
    trk[song.sel].eng_req = ENGI_FM6;
    fm6_load_slot(song.sel, 3);
    trk[song.sel].p[P_E7] = 3;                           /* (SLOT F4, as a load sets it: (FM6 LAB) a factory patch keeps
                                                          * its name when a sound is saved) */
    fail_after = (int)((sizeof(up_bank_t) + 255u) / 256u + 1u + 3u);   /* the preset bank's write passes, the patches' fails */
    up_store(20, "CUT");
    fail_after = -1;
    mig_boot();
    bad += check("a cut while a preset's patch is written: the others kept, that preset without one",
                 mig_ok() && upf_get(20, pk));
    up_store(20, "SAVED");
    mig_boot();
    bad += check("saved again: its patch, and the bank's sectors now both hold the patches object",
                 mig_ok() && !upf_get(20, pk) && !memcmp(pk + 118, FM6_FACTORY[3] + 118, 10) &&
                     ((st_hdr_t *)(nor + T_FM6_A))->type == OBJ_UPFM6 && ((st_hdr_t *)(nor + T_FM6_B))->type == OBJ_UPFM6);
    memset(nor, 0xFF, sizeof nor);                        /* a device that never had a bank: nothing written */
    before = erases;
    mig_boot();
    bad += check("no bank: nothing to move, nothing written", erases == before && upf_valid(&upf));
    return bad;
}

/* ---- 1.2, Discussion #130: the autosave (project.c): written only when the music changed, stopped and idle 10 s, at
 * most one a minute; restored at power-on (RESTORE LAST ON, a clean boot); a damaged one ignored ---- */
static void as_run(uint32_t ms)                          /* the main loop for ms (autosave_poll checks every 250 ms) */
{
    uint32_t end = fm1_ms + ms;
    while ((int32_t)(end - fm1_ms) > 0) {
        fm1_ms += 50u;
        autosave_poll();
    }
}
static void as_power_on(void)                            /* RAM lost: the defaults, then main.c's restore */
{
    uint32_t k;
    memset(trk, 0, sizeof trk);
    memset(&motion, 0, sizeof motion);
    memset(motion_active, 0, sizeof motion_active);
    memset(&as, 0, sizeof as);
    memset(&ui, 0, sizeof ui);
    proj_name[0] = 0;
    host_tracks_init();
    for (k = 0; k < NTRK; k++)
        trk[k].eng_req = trk[k].engine = (uint8_t)TRK_DEF[k][0];
    bank_boot();                                         /* (the patterns: flash's; the lists: the restore's) */
    lists_clear();
    motion_lost = 0;
}
static void as_edit(uint32_t i)                          /* a change a project holds: step i's note (always another) */
{
    static uint32_t n;
    track_t *t = &trk[i % NTRK];
    step_t *s = &t->step[i % 16u];
    s->time = ST_NOTE;
    s->n = 1;
    s->note[0] = (uint8_t)(20u + (s->note[0] + 1u + n++ % 50u) % 100u);
    s->vel = 100;
}
static int autosave_test(void)
{
    int bad = 0, ok;
    uint32_t w, k, b0, minute;
    step_t st[NSTEP];
    project_store_t raw;
    reset();
    as_power_on();
    as_erases = pool_erases = 0;
    ok = !autosave_boot(1) && !as.writes;
    as_run(120000u);
    bad += check("autosave: power-on with none: nothing restored; 2 min unchanged: no write", ok && !as.writes && !as_erases);
    as_edit(0);
    as_run(9000u);
    ok = !as.writes;
    as_run(1500u);
    bad += check("autosave: a change: no write within 10 s, one once idle 10 s (one erase)",
                 ok && as.writes == 1u && as_erases == 1u && st_load(OBJ_AUTOSAVE, &raw, sizeof raw) == (int)PROJ_SKF->size &&
                 pool_erases == 1u && bank_used(0, 0));   /* (phase 5: T1's steps a pattern first, then the project) */
    as_run(600000u);
    bad += check("autosave: 10 min unchanged after it: no more writes", as.writes == 1u && as_erases == 1u);
    song.g[G_SLOT] = 3;                                  /* (the PROJECT page's pick, the selected track: not the music) */
    song.sel = 2;
    as_run(120000u);
    bad += check("autosave: the selected track / PROJECT's slot pick alone: no write", as.writes == 1u);
    song.g[G_BPM] = 133;
    song.playing = 1;
    as_run(300000u);
    ok = as.writes == 1u;
    song.playing = 0;
    as_run(9000u);
    ok &= as.writes == 1u;
    as_run(1500u);
    bad += check("autosave: changed while playing: no write until stopped and idle 10 s, then one", ok && as.writes == 2u);
    as_edit(1);
    fm1_in.notes = 1u << 7;                              /* a key held (playing the sound, stopped) */
    as_run(60000u);
    ok = as.writes == 2u;
    fm1_in.notes = 0;
    trk[1].v[0].active = 1;                              /* .. its release still sounding */
    as_run(20000u);
    ok &= as.writes == 2u;
    trk[1].v[0].active = 0;
    as_run(9000u);
    ok &= as.writes == 2u;
    as_run(1500u);
    bad += check("autosave: a key held, a voice sounding: no write (an erase stops the audio); after, idle 10 s: one",
                 ok && as.writes == 3u);
    as_run(60000u);
    as_edit(2);                                          /* written 10 s on; then edits 15 s apart: a minute's gap */
    as_run(11000u);
    w = as.writes;
    as_edit(3);
    as_run(15000u);
    ok = as.writes == w;
    as_edit(4);
    as_run(15000u);
    ok &= as.writes == w;
    as_run(40000u);
    bad += check("autosave: two writes at least 60 s apart", ok && as.writes == w + 1u);
    as_run(60000u);
    w = as.writes;
    as_edit(5);
    for (k = 0; k < 120u; k++) {                         /* an editor backup going on: holds it */
        as_run(500u);
        autosave_hold();
    }
    ok = as.writes == w;
    as_run(10500u);
    bad += check("autosave: none during an editor transfer (autosave_hold), one after", ok && as.writes == w + 1u);
    /* power-on: restored */
    as_edit(6);
    song.g[G_BPM] = 97;
    str_cpy(proj_name, "MY TUNE", sizeof proj_name);
    as_run(70000u);
    memcpy(st, trk[0].step, sizeof st);
    b0 = as_erases;
    as_power_on();
    ok = memcmp(st, trk[0].step, sizeof st) != 0 && song.g[G_BPM] != 97;
    ok &= autosave_boot(1) == 1 && !memcmp(st, trk[0].step, sizeof st) && song.g[G_BPM] == 97 && str_eq(proj_name, "MY TUNE") &&
          ui.msg_t && str_eq(ui.msg, "RESTORED");
    as_run(300000u);
    bad += check("autosave: power-on restores it (steps, tempo, name; RESTORED); no write after (unchanged)",
                 ok && !as.writes && as_erases == b0);
    as_power_on();
    ok = autosave_boot(0) == 0 && song.g[G_BPM] != 97;
    as_run(60000u);
    bad += check("autosave: after a crash / hang / failed boot: not restored, nothing written", ok && as_erases == b0);
    as_power_on();
    ui_prefs |= PREF_RESTORE_OFF;
    ok = autosave_boot(1) == 0 && song.g[G_BPM] != 97;
    as_edit(7);
    w = pool_erases;
    as_run(300000u);
    bad += check("autosave: RESTORE LAST OFF: none restored, none written", ok && !as.writes && as_erases == b0);
    {   /* (phase 5) the patterns are kept all the same: edit 7's track (T4 of four) its pattern's */
        static step_t st4[NSTEP];
        static motion_store_t ms4;
        int16_t v4[5];
        bad += check("  .. the patterns kept all the same (T4's new steps a pattern, written)", pool_erases == w + 1u &&
                     bank_used(7u % NTRK, 0) && !pat_decode(&trk[7u % NTRK], ent_pat(7u % NTRK, 0), st4, v4, &ms4) &&
                     !memcmp(st4, trk[7u % NTRK].step, sizeof st4));
    }
    ui_prefs &= (uint8_t)~PREF_RESTORE_OFF;
    {   /* a damaged copy: the other one (A/B); both damaged: none */
        st_hdr_t h;
        int cur, other;
        as_power_on();
        autosave_boot(1);
        song.g[G_BPM] = 111;                             /* a newer copy into the other sector */
        as_run(11000u);
        cur = st_current(OBJ_AUTOSAVE, &h);
        nor[st_sector(OBJ_AUTOSAVE, (uint32_t)cur) + ST_PAYLOAD_OFF + 100u] ^= 0x10u;   /* its payload: the CRC fails */
        other = st_current(OBJ_AUTOSAVE, &h);
        as_power_on();
        ok = cur >= 0 && other == !cur && autosave_boot(1) == 1 && song.g[G_BPM] == 97;
        bad += check("autosave: the newest copy damaged: the copy before it is restored", ok);
        nor[st_sector(OBJ_AUTOSAVE, (uint32_t)other) + 12u] ^= 0x01u;   /* the other one's header too */
        as_power_on();
        ok = autosave_boot(1) == 0 && song.g[G_BPM] != 97 && song.g[G_BPM] != 111;
        bad += check("autosave: both copies damaged: ignored, power-on as new", ok);
        memset(nor + T_AS, 0xFF, 0x2000u);
        st_save(OBJ_AUTOSAVE, "NOT A PROJECT", 13u);     /* a valid record that is no project */
        as_power_on();
        bad += check("autosave: a record that is no project: ignored", autosave_boot(1) == 0 && song.g[G_BPM] != 97);
    }
    {   /* (phase 6) an autosave of four tracks (SKF2: the firmware before): restored, the last track as at power-on;
         * the next write the format of now (SKF3 with six tracks) */
        static project_t pj;
        static project_store_t w;
        uint32_t k = NTRK - 1u;
        memset(nor + T_AS, 0xFF, 0x2000u);
        as_power_on();
        song.g[G_BPM] = 123;
        project_capture(&pj);
        ok = proj_pack2(&w, &pj, &PROJ_SKF2) && !st_save(OBJ_AUTOSAVE, &w, PROJ_STORE2);
        as_power_on();
        song.g[G_BPM] = 120;
        trk[k].eng_req = ENGI_FM6;
        ok &= autosave_boot(1) == 1 && song.g[G_BPM] == 123 && trk[k].eng_req == trk_def_engine(k);
        as_edit(9);
        as_run(11000u);
        ok &= st_load(OBJ_AUTOSAVE, &w, sizeof w) == (int)PROJ_SKF->size && *(const uint32_t *)w.raw == PROJ_SKF->magic;
        bad += check("autosave: an SKF2 one restored (the last track as at power-on); written again as SKF2 / SKF3", ok);
    }
    /* a power cut in the middle of a write: the copy before stays in charge */
    memset(nor + T_AS, 0xFF, 0x2000u);
    as_power_on();
    autosave_boot(1);
    as_edit(8);
    as_run(11000u);                                      /* (written: copy A) */
    song.g[G_BPM] = 150;
    fail_after = 3;                                      /* the next write stops after 3 programs */
    as_run(70000u);
    fail_after = -1;
    ok = as.err;
    as_power_on();
    ok &= autosave_boot(1) == 1 && song.g[G_BPM] != 150 && trk[8u % NTRK].step[8].n == 1u;   /* (as_edit's track) */
    bad += check("autosave: a write cut short (power off): the copy before is restored", ok);
    /* wear: a 4-hour session: 3 h of editing (a change every 3 s), pauses and playing it back, then an hour away */
    as_power_on();
    autosave_boot(1);
    as_erases = 0;
    for (minute = 0; minute < 240u; minute++) {
        uint32_t s;
        if (minute >= 180u) {
            as_run(60000u);
            continue;
        }
        song.playing = minute % 10u >= 7u;               /* 3 minutes in 10 playing */
        for (s = 0; s < 60u; s += 3u) {
            if (minute % 5u != 4u)                       /* (every fifth minute a pause, no edits) */
                as_edit(minute * 20u + s);
            as_run(3000u);
        }
    }
    song.playing = 0;
    as_run(20000u);
    printf("persist:   a 4-hour session (3 h editing and playing, 1 h idle): %u autosave writes\n", (unsigned)as_erases);
    bad += check("autosave wear: a session's writes stay far below one a minute", as_erases > 0u && as_erases <= 60u);
    {   /* worst case: a change, then just enough idle, over and over, for 8 hours */
        uint32_t i;
        as_erases = pool_erases = 0;
        for (i = 0; i < 8u * 3600u / 11u; i++) {
            as_edit(i);
            as_run(11000u);
        }
        printf("persist:   worst case, 8 h of a change every 11 s: %u writes (%u per sector)\n", (unsigned)as_erases,
               (unsigned)(as_erases / 2u));
        bad += check("autosave wear, worst case: at most one write a minute (480 in 8 h)", as_erases <= 8u * 60u + 1u);
        {   /* (phase 5) the tracks' patterns: their chunks each written once a round, round their sectors + 4 spare */
            uint32_t k, c, n = 0, seen = 0;
            for (k = 0; k < NTRK; k++)
                if (trk_pid[k] && !((seen >> (c = bk_chunk(trk_pid[k] - 1u))) & 1u)) {
                    seen |= 1u << c;
                    n++;
                }
            printf("persist:   .. and %u pattern pool erases, the tracks' in %u chunks (about %u per sector)\n",
                   (unsigned)pool_erases, (unsigned)n, (unsigned)(pool_erases / (n + 4u)));
            bad += check("  (phase 5) the patterns too: a round a minute, each chunk changed one write a round",
                         n && pool_erases > 0u && pool_erases <= as_erases * n);
        }
    }
    return bad;
}

int main(void)
{
    int bad = 0, ok;
    uint32_t before;
    persist_t p;
    project_store_t old, got;
    project_v5_t v5;
    up_bank_t bank;
    up_rec_t r;
    reset();
    settings_init();
    {
        const uint8_t amber[] = {0, 2};
        song.playing = 1;
        bad += check("editor display change while playing is applied and reports deferred saving",
                      ed_ui_set(amber, sizeof amber) == 4 && settings.palette == 2u && persist_pending == 1 && !erases);
        song.playing = 0;
        settings_poll();
        bad += check("STOP commits the deferred palette setting",
                      !persist_pending && erases == 1 && persist_saved.palette == palette_to_stored(2));
        before = erases;
        bad += check("repeating an unchanged editor preference succeeds without erasing",
                      ed_ui_set(amber, sizeof amber) == 0 && erases == before);
        erase_error = 1;
        const uint8_t mono[] = {0, 0};
        bad += check("editor reports failed saving while preserving the previous flash setting",
                      ed_ui_set(mono, sizeof mono) == 3 && settings.palette == 0u && persist_pending == 2 &&
                      persist_saved.palette == palette_to_stored(2));
        erase_error = 0;
        fm1_ms += 1000;
        settings_poll();
        bad += check("failed editor preference retries through the existing persistence path",
                      !persist_pending && persist_saved.palette == palette_to_stored(0));
        const uint8_t bold[] = {1, 1};
        bad += check("the retired font weight is refused without a write", ed_ui_set(bold, sizeof bold) == 2);
    }
    {   /* (Skiff #27) THEME CUSTOM's set over COLORS: applied at once; while playing written at STOP, as UI_SET's; what a
         * power cycle reads (the settings object in flash, as persist_boot) is the set in use, in its look (the THEME
         * firmware before shows); REMOVE is written too */
        uint8_t a[2 + 26] = {1, UI_QUARK_INDEX}, raw[22];
        const uint8_t rm[1] = {2};
        uint16_t bg;
        uint32_t i, k;
        int n;
        for (i = 0; i < 22u; i++) raw[i] = (uint8_t)(37u * i + 11u);
        for (i = 0; i < 22u; i += 7u) {                 /* (pack7: a group's top bits, then its bytes) */
            uint8_t *g = a + 2u + i / 7u * 8u;
            for (k = 0; k < 7u && i + k < 22u; k++) { g[0] |= (uint8_t)(raw[i + k] >> 7 << k); g[1 + k] = raw[i + k] & 127u; }
        }
        bg = (uint16_t)(raw[0] | raw[1] << 8);
        favorites.factory[15][24] = 0xA5u;                 /* (SCENE_FREED: every unit since phase 5) */
        song.playing = 1;
        ok = ed_colors(a, sizeof a) == 4 && cpal_on() && T_BG == bg && ux.quark && persist_pending == 1;
        song.playing = 0;
        settings_poll();
        ok &= !persist_pending && persist_saved.palette == palette_to_stored(UI_QUARK_INDEX) &&
              persist_saved.favorites.factory[15][14] == (3u | UI_QUARK_INDEX << 2) &&
              !memcmp(&persist_saved.favorites.factory[14][8], raw, sizeof raw) && !memcmp(CPAL_COLS, raw, sizeof raw);
        memset(&favorites, 0, sizeof favorites); memset(&settings, 0, sizeof settings);   /* (a power cycle) */
        memset(&persist_saved, 0, sizeof persist_saved);
        ux_cpal = 0; palette_set(0);
        n = st_load(OBJ_SETTINGS, &p, sizeof p);
        ok &= settings_import(&p, n) == 1 && !settings_init() && cpal_on() && settings.palette == UI_QUARK_INDEX &&
              T_BG == bg && ux.quark;
        persist_saved = p;
        before = erases;
        ok &= ed_colors(rm, 1) == 0 && erases > before && !cpal_stored() && T_BG == UI_PALETTES[UI_QUARK_INDEX].bg &&
              persist_saved.favorites.factory[15][14] == 0u && persist_saved.palette == palette_to_stored(UI_QUARK_INDEX);
        bad += check("(#27) COLORS: stored while playing, written at STOP, in use after a power cycle; REMOVE written", ok);
    }
#if BOARD_SMK37
    {   /* (SMK-37 Pro: no LEDS row, so the editor's id 4 is unknown there; the MENU_SET path below is the FM-1's) */
        const uint8_t leds_off[] = {4, 0, 64};
        reset();
        settings_init();
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, leds_off, 3);
        bad += check("SMK-37: MENU_SET of LEDS: rc 1 (no such row), nothing changed, no erase",
                     ed_rep[0] == 1 && settings_leds != LEDS_OFF && !persist_pending && !erases);
    }
#else
    {   /* MENU_SET (editor_menu.c): the same settings record, the same deferral, retry and idempotence */
        const uint8_t leds_off[] = {4, 0, 64}, large_on[] = {2, 1, 64}, hold_06[] = {5, 3, 64}, eq_bass[] = {9, 2, 64};
        persist_t q;
        reset();
        settings_init();
        song.playing = 1;
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, leds_off, 3);
        bad += check("MENU_SET while playing: applied, rc 4 (queued until STOP), no erase",
                     ed_repn == 4 && ed_rep[0] == 4 && ed_rep[1] == 4 && settings_leds == LEDS_OFF && persist_pending == 1 && !erases);
        song.playing = 0;
        settings_poll();
        bad += check("STOP commits the MENU setting (LEDS OFF in the settings record)",
                     !persist_pending && erases == 1 && st_load(OBJ_SETTINGS, &q, sizeof q) == (int)sizeof q &&
                     leds_from_stored(q.zoom) == LEDS_OFF);
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, large_on, 3);
        ok = ed_rep[0] == 0 && (ui_prefs & PREF_LARGE);
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, hold_06, 3);
        ok &= ed_rep[0] == 0 && settings_hold == 3u;
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, eq_bass, 3);
        ok &= ed_rep[0] == 0 && st_load(OBJ_SETTINGS, &q, sizeof q) == (int)sizeof q && q.lowcut == 2u &&
              hold_from_stored(q.bold) == 3u && (q.favorites.factory[15][30] & PREF_LARGE) && leds_from_stored(q.zoom) == LEDS_OFF;
        bad += check("MENU_SET stopped: rc 0, saved at once (LARGE, HOLD, SPEAKER EQ), the others kept", ok);
        before = erases;
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, hold_06, 3);
        bad += check("repeating an unchanged MENU_SET: rc 0, no erase", ed_rep[0] == 0 && erases == before);
        erase_error = 1;
        ed_repn = 0; ed_menu_handle(ED_MENU_SET, leds_off, 3);   /* unchanged: still rc 0 (no write) */
        ok = ed_rep[0] == 0;
        {
            const uint8_t leds_inv[] = {4, 3, 64};
            ed_repn = 0; ed_menu_handle(ED_MENU_SET, leds_inv, 3);
        }
        ok &= ed_rep[0] == 3 && settings_leds == LEDS_INV && persist_pending == 2 && leds_from_stored(persist_saved.zoom) == LEDS_OFF;
        bad += check("MENU_SET with a failing flash: applied, rc 3, the saved setting kept", ok);
        erase_error = 0;
        fm1_ms += 1000;
        settings_poll();
        bad += check("... and retried by the settings path", !persist_pending && leds_from_stored(persist_saved.zoom) == LEDS_INV);
        settings_hold = HOLD_DEF; settings_leds = LEDS_DIM;
    }
#endif
    reset();
    settings.palette = 3;
    settings.lowcut = 2;
    settings.zoom = 1;
    persist_saved.bold = 1;                       /* (an earlier firmware's font weight: kept as saved) */
    favorites.factory[8][0] = 1;
    favorites.user = 1u << 31;
    song.playing = 1;
    settings_save();
    bad += check("playing: settings queued, no erase, runtime kept", persist_pending && !erases &&
                  settings.palette == 3u && settings.lowcut == 2u && settings.zoom == 1u);
    song.playing = 0;
    transport_req = 1;
    settings_poll();
    bad += check("pending PLAY: settings still queued, no erase", persist_pending && !erases);
    transport_req = 0;
    settings.palette = 2;
    settings_poll();
    bad += check("STOP: latest settings saved and request cleared", !persist_pending && erases == 1u &&
                  st_load(OBJ_SETTINGS, &p, sizeof p) == (int)sizeof p && p.palette == palette_to_stored(2) &&
                  p.lowcut == 2u && p.zoom == 1u && p.bold == 1u &&
                  p.favorites.factory[8][0] == 1 && p.favorites.user == (1u << 31) &&
                  !memcmp(&p.panel, &panel, sizeof panel));
    settings_save();
    bad += check("unchanged settings do not erase", !persist_pending && erases == 1u);
    settings.palette = 1;
    erase_error = 1;
    fm1_ms = 0xFFFFFF00u;
    settings_save();
    before = erases;
    settings_poll();
    fm1_ms += 999u;
    settings_poll();
    bad += check("failed settings retry waits through clock wrap", persist_pending && erases == before &&
                  persist_saved.palette == palette_to_stored(2) && settings.palette == 1u);
    erase_error = 0;
    fm1_ms++;
    settings_poll();
    bad += check("settings retry succeeds after 1000 ms", !persist_pending && erases == before + 1u &&
                  st_load(OBJ_SETTINGS, &p, sizeof p) == (int)sizeof p && p.palette == palette_to_stored(1));

    reset();
    trk[0].step[0] = (step_t){{60}, 1, ST_NOTE, 0, 96, 0, 0};
    project_save(0);
    old = proj_slot[0];
    song.g[G_BPM] = 140;                              /* (the tracks as kept: the project's write alone fails) */
    fail_after = 1;
    project_save(0);
    fail_after = -1;
    bad += check("failed project save keeps RAM and old flash", !strcmp(ui.msg, "SAVE ERROR") &&
                  !memcmp(&proj_slot[0], &old, sizeof old) && st_load(OBJ_PROJECT0, &got, sizeof got) == (int)PROJ_SKF->size &&
                  !memcmp(&got, &old, PROJ_SKF->size));
    trk[0].step[0].note[0] = 72;
    project_save(0);
    {   /* (phase 5) SKF2: the steps its patterns', kept first */
        static step_t st0[NSTEP];
        static motion_store_t ms0;
        int16_t v0[5];
        bad += check("successful project save publishes new RAM (the tracks' patterns kept first)", !strcmp(ui.msg, "SAVED") &&
                      proj_import(&proj_scratch, &proj_slot[0], sizeof proj_slot[0]) && proj_scratch.g[G_BPM] == 140 &&
                      proj_ok(&proj_scratch) && proj_scratch.ent[0][0][0] == ent_pat(0, 0) + 1u &&
                      !pat_decode(&trk[0], ent_pat(0, 0), st0, v0, &ms0) && st0[0].note[0] == 72u);
    }
    before = erases;
    transport_req = 1;
    project_save(1);
    bad += check("pending PLAY: project save leaves slot and flash", !project_used(1) && erases == before &&
                  !strcmp(ui.msg, "STOP TO SAVE"));
    transport_req = 0;
    erase_error = 1;
    project_save(2);
    bad += check("failed first project save leaves slot empty", !project_used(2));
    erase_error = 0;
    {   /* (1.2.1, #6) SAVE > PROJECT's DELETE: an empty record in flash and the RAM copy cleared, so the slot reads
         * empty after a fetch from flash too (a warm reset keeps .noinit RAM); a failed write keeps the slot */
        project_save(3);
        ok = project_used(3);
        erase_error = 1;
        ok &= project_erase(3) == 2 && project_used(3) && !strcmp(ui.msg, "DELETE ERROR");
        erase_error = 0;
        ok &= project_erase(3) == 0 && !project_used(3) && !strcmp(ui.msg, "PROJECT DELETED");
        proj_fetch(3);
        ok &= !project_used(3) && st_load(OBJ_PROJECT0 + 3u, &got, sizeof got) == 0;
        bad += check("(#6) DELETE: the project slot empty in RAM and flash; a failed write keeps it", ok);
    }
    {   /* names: a project's through flash; a failed rename keeps the old one in RAM and flash */
        char n[16];
        project_store_t keep;
        step_t st0 = trk[0].step[0];
        project_save_as(0, "LOFI JAM");
        memset(proj_slot[0].raw, 0, 8);                       /* (the RAM copy lost: read it back from flash) */
        proj_fetch(0);
        bad += check("a project's name goes to flash and comes back", project_name(0, n) && !strcmp(n, "LOFI JAM") &&
                      !strcmp(proj_name, "LOFI JAM"));
        keep = proj_slot[0];
        fail_after = 1;
        bad += check("failed project rename: SAVE ERROR, the slot and flash as they were",
                      project_rename(0, "DUB") == 2 && !strcmp(ui.msg, "SAVE ERROR") && !memcmp(&proj_slot[0], &keep, sizeof keep) &&
                      st_load(OBJ_PROJECT0, &got, sizeof got) == (int)PROJ_SKF->size && !memcmp(&got, &keep, PROJ_SKF->size));
        fail_after = -1;
        before = erases;
        transport_req = 1;
        bad += check("pending PLAY: no rename, no erase", project_rename(0, "DUB") == 1 && erases == before &&
                      !strcmp(ui.msg, "STOP TO SAVE"));
        transport_req = 0;
        bad += check("project rename: flash has the new name, the music as it was (SKF2 still: its lists)",
                      project_rename(0, "DUB") == 0 &&
                      !strcmp(ui.msg, "RENAMED") && (memset(proj_slot[0].raw, 0, 8), proj_fetch(0), project_name(0, n)) &&
                      !strcmp(n, "DUB") && *(uint32_t *)proj_slot[0].raw == PROJ_SKF->magic &&
                      !memcmp(proj_scratch.ent, lists.ent, sizeof lists.ent) && proj_scratch.ent[0][lists.cur[0]][0] &&
                      st0.note[0] == trk[0].step[0].note[0]);
    }
    {   /* FUN7 itself refuses a step outside its fields: a damaged slot is empty, the tracks stay */
        step_t keep = trk[0].step[0];
        old = proj_slot[0];
        project_capture(&proj_scratch);                           /* (an SKF1 slot: the format before phase 5's) */
        proj_pack(&proj_slot[0], &proj_scratch);
        proj_slot[0].raw[68 + PROJ_NP + 2 + 4] = 7;               /* track 1 step 1: n = 7 */
        bad += check("damaged FUN7 slot (hash ok, n > 4) is refused", (memcpy(proj_slot[0].raw + PROJ_STORE_SIZE - 4,
                      &(uint32_t){proj_hash(proj_slot[0].raw, PROJ_STORE_SIZE - 4)}, 4), !project_used(0)));
        st_save(OBJ_PROJECT0, &proj_slot[0], sizeof proj_slot[0]);
        ui.msg[0] = 0;
        project_load(0);
        bad += check("loading it says EMPTY SLOT and keeps the steps", !strcmp(ui.msg, "EMPTY SLOT") &&
                      !memcmp(&trk[0].step[0], &keep, sizeof keep));
        proj_slot[0] = old;
        st_save(OBJ_PROJECT0, &proj_slot[0], sizeof proj_slot[0]);
    }
    {   /* an older format (FUN5) with values FUN7 cannot pack still loads, bounded */
        uint32_t i;
        memset(&v5, 0, sizeof v5);
        v5.magic = PROJ_MAGIC_V5;
        v5.size = sizeof v5;
        for (i = 0; i < G_COUNT; i++)
            v5.g[i] = GP[i].def;
        for (i = 0; i < PROJ_NT; i++) {
            v5.t[i].engine = trk[i].engine;
            memcpy(v5.t[i].p, trk[i].p, sizeof v5.t[i].p);
        }
        v5.t[0].p[P_LEVEL] = 900;                                  /* out of range */
        v5.t[0].step[0] = (step10_t){{255, 128, 160, 200}, 255, 255, 0, 96, 255, 5};
        v5.sum = proj_hash(&v5, sizeof v5 - 4u);
        st_save(OBJ_PROJECT0 + 3u, &v5, sizeof v5);
        memset(proj_slot[3].raw, 0, 4);
        project_load(3);
        bad += check("FUN5 load bounds steps and masks lane accents", trk[0].step[0].n == 4u &&
                      trk[0].step[0].time == ST_REST && trk[0].step[0].note[0] == 127u &&
                      !trk[0].step[0].note[1] && trk[0].step[0].acc == 5u && project_used(3));
        bad += check("FUN5 load clamps a parameter into its range", trk[0].p[P_LEVEL] == TP[P_LEVEL].max);
        for (i = 0; i < P_COUNT; i++) {
            uint32_t e;
            for (e = 0; e < NENGINES; e++)
                if (param_desc_of(e, i)->min < -64 || param_desc_of(e, i)->max > 127) {
                    printf("param %u engine %u: range %d..%d does not fit FUN7's byte\n", i, e,
                           param_desc_of(e, i)->min, param_desc_of(e, i)->max);
                    bad++;
                }
        }
    }
    transport_req = 0;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.ver = UP_VER;
    r.np = P_COUNT;
    memcpy(r.name, "First", 6);
    erase_error = 1;
    bank = up_bank[1];
    ok = up_put(17, &r) == 2;
    erase_error = 0;
    bad += check("failed first preset save restores empty bank", ok && !memcmp(&up_bank[1], &bank, sizeof bank));
    bad += check("preset initial save", up_put(3, &r) == 0 && up_used(3));
    bank = up_bank[0];
    before = up_gen;
    r.p[0] = 77;
    fail_after = 1;
    ok = up_put(3, &r) == 2;
    fail_after = -1;
    bad += check("failed preset save rolls back bank and generation", ok && up_gen == before &&
                  !memcmp(&up_bank[0], &bank, sizeof bank));
    favorite_set(NENGINES, 3, 1);
    fail_after = 1;
    ok = up_put(3, 0) == 2;
    fail_after = -1;
    bad += check("failed preset erasure retains both the sound and its favorite", ok && up_used(3) &&
                  favorite_has(NENGINES, 3) && !memcmp(&up_bank[0], &bank, sizeof bank));
    memset(&up_bank[0], 0, sizeof up_bank[0]);
    up_boot();
    bad += check("failed preset save keeps old flash", !memcmp(&up_bank[0], &bank, sizeof bank));
    trk[0].user = 4;
    erase_error = 1;
    ok = up_put(3, 0) == 2;
    erase_error = 0;
    bad += check("failed preset erase keeps record and loaded label", ok && trk[0].user == 4u && up_gen == before &&
                  !memcmp(&up_bank[0], &bank, sizeof bank));
    bad += check("successful preset erase clears loaded label", up_put(3, 0) == 0 && !up_used(3) && !trk[0].user);
    before = erases;
    transport_req = 1;
    bad += check("pending PLAY: preset save blocked", up_put(3, &r) == 2 && erases == before && !up_used(3));
    transport_req = 0;
    bad += check("preset invalid slot blocked before access", up_put(UP_SLOTS, &r) == 1 && erases == before);
    song.playing = 0; transport_req = 1; irq_start_race = 1;
    bad += check("PLAY consumed before the save snapshot still blocks a project write",
                  project_save(1) != 0 && irq_races == 1u && song.playing && !transport_req && erases == before);
    song.playing = 0; transport_req = 1; irq_start_race = 1;
    bad += check("PLAY consumed before the preset snapshot still blocks a bank write",
                  up_put(3, &r) == 2 && irq_races == 2u && song.playing && !transport_req && erases == before);
    song.playing = 0; transport_req = 1; irq_start_race = 1;
    settings.palette = 4;
    settings_save();
    bad += check("PLAY consumed before the settings snapshot defers the write",
                  irq_races == 3u && song.playing && !transport_req && persist_pending && erases == before);
    bad += mig_test();
    bad += autosave_test();
    printf("%s\n", bad ? "PERSISTENCE TEST FAILED" : "persistence test passed");
    return bad != 0;
}
