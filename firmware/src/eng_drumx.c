/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * (Skiff, modified 2026-10-10) DRUM-X: the drum engine of JIANT FM by juanjiant-bit,
 * https://github.com/juanjiant-bit/JIANT (GPL-3.0-only, its eng_drum.c at 9d1c453). In JIANT it replaced DRUM; here it
 * is an engine of its own beside DRUM, without JIANT's group mutes, X-MOD, PUNCH, ATK+ and perform FX. */
/* DRUM-X (drumx_voice.c, after Sonic Charge's Microtonic): a kit of 8 lanes, KICK SNARE CLAP HAT CL HAT OP TOM RIM
 * BELL, each a synthesized sound with two patches A and B and a MORPH between them. The kit is the project's
 * (drumx_voice.c dx_kit: one for every DRUM-X track, project.c keeps it in SKF3), edited on EDIT > SOUND (ui_dx.c).
 *
 * A drum track as DRUM's is (seq.c drum_track): the same keys (the General MIDI drum map, eng_drum.c drum_keys),
 * lanes (drum_lane), grid and lane LEVELs (P_LN0..P_LN7, EDIT > LANES), and the same voices: a lane per drum, mono
 * (a hit reuses its lane's voice even at another pitch), the 8 lanes ringing together, a closed hat choking the open
 * one, a released key not ending a hit, the hit the envelope (engine_t.oneshot: what applies is DRUM's).
 *
 * Parameters (P_E0..P_E7): MRPH the MORPH A..B of every lane; TUNE (+-12 semitones), TONE (COLOR), DECY (DECAY) and
 * NOIS (NOISE) move every lane from its patch (64: as the kit has it); FM every sound through harmonic FM in eight
 * ratio bands (0: the patch as it is); FOLD every sound's oscillator through a wavefolder after its envelope; DRV a
 * soft clip on every hit (x1..x4, the level kept). The knobs move a hit while it rings.
 *
 * State: 8 lanes per part in the part's shared engine area (engines.c eng_mem, eng_lite): each lane's hit. */
#include "drumx_voice.c"

/* engine e plays a kit (DRUM, DRUM-X): a drum track (seq.c drum_track), its kick the duck's source (duck.c) */
static inline int drum_engine(uint32_t e) { return e == ENGI_DRUM || e == ENGI_DRUMX; }

/* General MIDI notes 35..81 -> the lane (DV_*, as drum_lane gives it) and semitones from the lane's pitch: congas on
 * TOM, claves on RIM, cymbals on BELL, each at its pitch from the lane's (JIANT's map) */
static const int8_t DX_GM[47][2] = {
    {DV_KICK, -2}, {DV_KICK, 0}, {DV_RIM, 0}, {DV_SNARE, 0}, {DV_CLAP, 0}, {DV_SNARE, 2},             /* 35 */
    {DV_TOM, -7}, {DV_HATC, 0}, {DV_TOM, -4}, {DV_HATC, -2}, {DV_TOM, 0}, {DV_HATO, 0},               /* 41 */
    {DV_TOM, 3}, {DV_TOM, 5}, {DV_BELL, 0}, {DV_TOM, 8}, {DV_BELL, -3}, {DV_BELL, 2},                 /* 47 */
    {DV_BELL, 5}, {DV_HATC, 5}, {DV_BELL, 4}, {DV_BELL, 0}, {DV_BELL, 1}, {DV_RIM, -12},              /* 53 */
    {DV_BELL, -2}, {DV_TOM, 10}, {DV_TOM, 7}, {DV_TOM, 5}, {DV_TOM, 5}, {DV_TOM, 0},                  /* 59 */
    {DV_TOM, 10}, {DV_TOM, 7}, {DV_BELL, 7}, {DV_BELL, 3}, {DV_HATC, 3}, {DV_HATC, 7},                /* 65 */
    {DV_RIM, 7}, {DV_RIM, 5}, {DV_HATC, -4}, {DV_HATC, -6}, {DV_RIM, 0}, {DV_RIM, -4},                /* 71 */
    {DV_RIM, -7}, {DV_TOM, 12}, {DV_TOM, 8}, {DV_BELL, 12}, {DV_BELL, 12},                            /* 77 */
};
static uint32_t dx_gm_ix(uint32_t note) { return (note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u) - 35u; }

typedef struct {
    dx_voice_t x;                /* the hit */
    uint8_t owner;               /* the voice playing the lane: index + 1, 0 = none */
    int8_t st;                   /* its semitones from the lane's pitch (the GM map) */
} drumx_lane_t;

static drumx_lane_t *drumx_kit_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (drumx_lane_t *)eng_part(t) : 0;
}

/* the lane voice v plays, 0 when it plays none (any more) */
static drumx_lane_t *drumx_lane_of(track_t *t, const voice_t *v)
{
    drumx_lane_t *K = drumx_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v);
    if (!K || i >= NVOICE)
        return 0;
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    return L->owner == i + 1u ? L : 0;
}

/* Different GM pitches on a lane still share its one sounding voice (voice.c voice_alloc). */
static voice_t *drumx_reuse(track_t *t, uint32_t note)
{
    drumx_lane_t *K = drumx_kit_of(t);
    uint32_t lane = drum_lane(note), owner = K ? K[lane].owner : 0;
    voice_t *v;
    if (!owner || owner > NVOICE)
        return 0;
    v = &t->v[owner - 1u];
    return v->active && (uint32_t)v->s[0] == lane ? v : 0;
}

static void drumx_note_on(track_t *t, voice_t *v)
{
    drumx_lane_t *K = drumx_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v), lane;
    if (!K || i >= NVOICE)
        return;
    lane = drum_lane(v->note);
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    if (L->owner == i + 1u)                              /* this voice played another lane: it stops there */
        L->owner = 0;
    L = &K[lane];
    L->st = DX_GM[dx_gm_ix(v->note)][1];
    L->owner = (uint8_t)(i + 1u);                        /* (its last voice, if another, ends: drumx_amp) */
    v->s[0] = (int32_t)lane;
    v->env_out = v->vel * 258;                           /* the hit starts at its level (no ramp from 0) */
    v->env = 1 << 24;                                    /* the ADSR held at full from here (eng_drum.c: a key-off
                                                          * before the first block must not end a fresh voice) */
    dx_trigger(&L->x);
    if (lane == DV_HATC)                                 /* a closed hat chokes the open one */
        dx_choke(&K[DV_HATO].x);
}

/* the voice's amplitude: the hit, not the ADSR (held at full so a release never ends it). A voice that plays no
 * lane any more, or whose hit has rung out, ends here */
static int32_t drumx_amp(track_t *t, voice_t *v, int32_t adsr)
{
    const drumx_lane_t *L = drumx_lane_of(t, v);
    (void)adsr;
    if (!v->active)                                      /* (taken for another part: env_tick ended it) */
        return 0;
    if (!L || (!L->x.live && !L->x.trig)) {
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return 32767;
}

static void drumx_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    drumx_lane_t *L = drumx_lane_of(t, v);
    uint32_t lane = (uint32_t)v->s[0] & (DV_NLANE - 1u), i;
    int32_t y[CTL], drv = p[P_E7], g = 0, mk = 0, lv = clamp(p[P_LN0 + lane], 0, 127);
    vmod_t ml;                                           /* (only its amplitude ramp is read: voice_amp) */
    if (!L)
        return;
    if (lv < 127) {                                      /* the lane's LEVEL (square law) on the block's amplitude
                                                          * ramp, as DRUM's; 100 %: the ramp as it was */
        int32_t gl = lv * lv * 2 + (lv * lv >> 6);
        ml.amp0 = (m->amp0 >> 4) * gl >> 11;
        ml.amp1 = (m->amp1 >> 4) * gl >> 11;
        m = &ml;
    }
    if (n > CTL)
        n = CTL;
    dx_run(&dx_kit[lane], &L->x, p[P_E0], L->st * 16 + (p[P_E1] - 64) * 3, p[P_E3] - 64, p[P_E2] - 64, p[P_E4] - 64,
           p[P_E6], p[P_E5], y, n);
    if (drv > 0) {                                       /* DRV: x1..x4 into the soft clip, the level kept (Q12) */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    for (i = 0; i < n; i++) {                            /* x1.25 and the knee below, in 32 bits (as DRUM's) */
        int32_t s = y[i];
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        out[i] += voice_amp(soft_knee(s + (s >> 2), 24000), m, i) << 1;
    }
}

/* {MRPH, TUNE, TONE, DECY, NOIS, FM, FOLD, DRV} */
#define DRUMX_E {64, 64, 70, 64, 64, 0, 0, 0}
static const preset_t DRUMX_PRESETS[] = {
    {"DRUM KIT", DRUMX_E, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20)},   /* (the footer: DRUM-X DRUM KIT) */
    /* (Skiff) for the themes: CRYPT (dungeon synth), NEON (synthwave), QUARK (particles) */
    {"HADRON KIT", {100, 72, 90, 40, 80, 70, 60, 25}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 30, 25)},
};

static const engine_t ENG_DRUMX = {
    .name = "DRUM-X",
    .page_title = {"KIT", "HIT"},
    .edit = {
        {"MORPH", F_PCT, 0, 127, 64, 0, 0},
        {"PITCH", F_ST16, 0, 127, 64, 0, 0},
        {"COLOR", F_PCT, 0, 127, 64, 0, 0},
        {"DECAY", F_PCT, 0, 127, 64, 0, 0},
        {"NOISE", F_PCT, 0, 127, 64, 0, 0},
        {"FM", F_PCT, 0, 127, 0, 0, 0},
        {"FOLD", F_PCT, 0, 127, 0, 0, 0},
        {"DRIVE", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = DRUMX_PRESETS,
    .npresets = NELEM(DRUMX_PRESETS),
    .note_on = drumx_note_on,
    .render = drumx_render,
    .amp = drumx_amp,
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .poly = DV_NLANE,
    .oneshot = 1,
    .keys = drum_keys,
};
