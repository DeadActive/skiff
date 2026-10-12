/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff: live pattern switching (DESIGN.md: the PERFORM layer, SAVE > PATTERNS). While playing, the main loop stages a
 * pattern from the bank for a track (bank.c bank_switch: launch_q, its steps, LEN DIV SWING GATE DIR, its automation and
 * locks) and arms it; the audio ISR swaps it in on a step boundary, at its launch point (the PERFORM layer's
 * KNOB 1, ui.c ui_launch):
 *   LOOP  where the track's pass ends (its last step in its direction: seq_pass_last)
 *   BAR   the step that begins on the next bar: the bars as the click counts them, four beats of four 1/16 steps as
 *         a 1/16 track counts its steps (launch_clock: launch_steps, counted before the tracks tick, so a 1/16 step on
 *         the bar line sees it). A track whose DIV rounds to steps a little shorter than 1/16's (1/32, the triplets:
 *         div_samples) runs a few samples a beat ahead of that: its step starting less than half a step before the
 *         bar line is the bar's too
 *   BEAT  likewise the next beat
 *   NOW   the next step, the playhead going on where it is
 * LOOP, BAR and BEAT start the pattern from its first step in its direction (a new pass). The swap exchanges: launch_q
 * then holds the pattern that played (done), which the main loop keeps as the pattern it was (bank_poll). The notes of
 * the pattern going out end; a NUDGE or RATCH part of it still to come is dropped.
 * Staged mutes (the PERFORM layer's black keys 1..4, bank.c mute_switch): a track's MUTE turned at its own launch
 * point the same way (LOOP: that track's pass end); the step starting there plays as the MUTE says.
 * Included by seq.c (after motion.c, before seq_tick); launch_step runs in the ISR, only with something staged, not
 * inlined. */
enum { LAUNCH_LOOP, LAUNCH_BAR, LAUNCH_BEAT, LAUNCH_NOW, LAUNCH_N };
static const uint8_t LAUNCH_TIME[5] = {P_SLEN, P_SDIV, P_SSWING, P_SGATE, P_DIR};
typedef struct {
    volatile uint8_t armed;                        /* staged, waiting for its launch point (set last; the ISR clears) */
    volatile uint8_t done;                         /* swapped in (the ISR; the main loop keeps what went out, clears it) */
    uint8_t slot, mode;                            /* the entry (the track's list: bank.c), LAUNCH_* */
    uint8_t adv;                                   /* a song's next one (bank.c song_poll): a stop drops it */
    uint8_t id, mlost;                             /* (phase 5) its pattern; 1: it comes without its automation */
    uint8_t reps;                                  /* .. the passes the one playing makes before it (a song's; 0 none) */
    uint32_t at;                                   /* BAR / BEAT: the launch_steps (a beat's, a bar's) it comes in on */
    step_t step[NSTEP];                            /* the pattern (after the swap: the one it replaced) */
    int16_t time[5];                               /* .. its LEN DIV SWING GATE DIR */
    motion_store_t motion;                         /* .. its automation and locks (place: the track's), its on bit */
} launch_t;
static launch_t launch_q[NTRK] __attribute__((section(".pool")));
static motion_event_t launch_in[MOTION_MAX] __attribute__((section(".pool")));   /* (the swap's: off the ISR stack) */
/* a mute staged for track k: 0 none, 1 unmuted, 2 muted at its launch point (mode, at). One byte a track, set by the
 * main loop (mute_switch, after mode and at), cleared by the ISR as it lands or by the main loop */
static volatile uint8_t mute_arm[NTRK];
static uint8_t mute_mode[NTRK];
static uint32_t mute_at[NTRK];

/* track t's playing step has just ended (period: its DIV's step; t->seq_pos: how long ago, at the block's end): a
 * launch point (mode, at) is here */
static int launch_here(const track_t *t, uint32_t len, uint32_t period, uint32_t mode, uint32_t at)
{
    if (mode == LAUNCH_LOOP)
        return seq_pass_last(t, len);
    if (mode == LAUNCH_NOW || launch_steps >= at)
        return 1;
    return (at - launch_steps) * div_samples(2) - launch_pos + t->seq_pos < period / 2u;   /* (its line < half a step on) */
}
/* .. its staged pattern comes in now (a song's: once the pattern playing has made its passes, take.c take_p0) */
static int launch_due(const track_t *t, uint32_t len, uint32_t period)
{
    uint32_t k = trk_index(t);
    const launch_t *q = &launch_q[k];
    if ((uint16_t)(take_pass[k] - take_p0[k]) < q->reps)
        return 0;
    return launch_here(t, len, period, q->mode, q->at);
}
/* (phase 5) the step track t enters may end where its staged pattern comes in (LOOP: its pass's last, the passes
 * made; BAR, BEAT, NOW: whenever one is staged, the point not known here): the step after it is not played early
 * (seq.c seq_enter: NUDGE), it would be the outgoing pattern's */
static int launch_ahead(const track_t *t, uint32_t len)
{
    uint32_t k = trk_index(t);
    const launch_t *q = &launch_q[k];
    if (!q->armed)
        return 0;
    return q->mode != LAUNCH_LOOP || (seq_pass_last(t, len) && (uint16_t)(take_pass[k] - take_p0[k]) >= q->reps);
}
/* the swap (the audio ISR): the track's steps, timing and motion for the staged ones, the staged ones for them */
static __attribute__((noinline)) void launch_swap(track_t *t)
{
    uint32_t k = trk_index(t), i, n;
    launch_t *q = &launch_q[k];
    motion_event_t *in = launch_in;
    uint32_t cin = q->motion.count, on = q->motion.on;
    memcpy(in, q->motion.event, cin * sizeof in[0]);
    seq_release(t);                                /* (the notes going out end; held or tied, too) */
    motion_restore(t);                             /* (the automation's values: the sound's own again) */
    q->motion.count = 0;
    q->motion.on = (uint8_t)(motion.on & (1u << k));
    for (i = 0, n = 0; i < motion.count; i++)
        if ((motion.event[i].place >> 6) == k)
            q->motion.event[q->motion.count++] = motion.event[i];
        else
            motion.event[n++] = motion.event[i];
    for (i = 0; i < cin && n < MOTION_MAX; i++)
        motion.event[n++] = in[i];
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion.on = (uint8_t)((motion.on & ~(1u << k)) | (on & (1u << k)));
    for (i = 0; i < NSTEP; i++) {
        step_t s = t->step[i];
        t->step[i] = q->step[i];
        q->step[i] = s;
    }
    for (i = 0; i < 5u; i++) {
        int16_t v = t->p[LAUNCH_TIME[i]];
        t->p[LAUNCH_TIME[i]] = q->time[i];
        q->time[i] = v;
    }
    t->rat_left = 0;
    t->nud = 0;
    t->rskip_n = 0;
    t->rh_n = 0;
    q->armed = 0;
    q->done = 1;
}
/* a project load (project.c, the transport stopped, the ISR off): nothing staged comes in over it (what went out at a
 * swap before it: kept first, project_load) */
static void launch_drop(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        launch_q[k].armed = launch_q[k].done = mute_arm[k] = 0;
}
/* track t's playing step has just ended, a mute or a pattern staged for it: at its launch point the mute turns; the
 * swap, and (but NOW) its first step next, a new pass. 1: swapped (seq_tick takes its DIV and LEN again). Not
 * inlined: seq_tick's step loop keeps its shape (tests/target_budget.py) */
static __attribute__((noinline)) int launch_step(track_t *t, uint32_t len, uint32_t period)
{
    uint32_t k = (uint32_t)(t - trk), now = launch_q[k].mode == LAUNCH_NOW;
    if (mute_arm[k] && launch_here(t, len, period, mute_mode[k], mute_at[k])) {
        t->p[P_MUTE] = (int16_t)(mute_arm[k] - 1u);
        mute_arm[k] = 0;
    }
    if (!launch_q[k].armed || !launch_due(t, len, period))
        return 0;
    launch_swap(t);
    take_p0[k] = take_pass[k];                     /* (phase 5) its passes from the next on (a song's repeats) */
    if (!now) {
        t->seq_idx = (uint16_t)(t->p[P_SLEN] > 1 ? t->p[P_SLEN] - 1 : 0);
        t->seq_cnt = 0;
    }
    return 1;
}
/* each block, before the tracks tick: the 1/16 steps as a 1/16 track (and the click: click_tick) counts them, PLAY's
 * first block step 0 (seq_start), the remainder kept (BAR / BEAT launches) */
static void launch_clock(uint32_t n)
{
    uint32_t p = div_samples(2);
    if (launch_pos == CLK_START) {
        launch_pos = 0;
        return;
    }
    launch_pos += n;
    while (launch_pos >= p) {
        launch_pos -= p;
        launch_steps++;
    }
}
