/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff W3) the editor's song (EDITOR_PROTOCOL.md "Pool, lists, PERFORM"): the pattern pool (SAVE > PATTERNS), the
 * tracks' lists (SEQ > SONG), PERFORM's scenes, staged mutes and LAUNCH; INFO 57 01 caps bits 2, 3, 4. Each acts as the
 * device's own control does, through the same functions (bank.c), and says so on the device. Included by editor.c.
 *   POOL: [1, id: CLEAR] -> op, rc (0 cleared, 1 nothing to clear, 2 in use, 3 playing, 4 failed), then each pattern:
 *     its LEN (0 empty), what uses it (bank.c pool_refs, at most 127)
 *   LIST: [op, track, entry ..]: 1 SET (pattern 1..64: a kept one), 2 COPY, 3 PLAY (the track to it), 4 CLEAR (stopped;
 *     its pattern goes when nothing else names it), 5 REPS (1..16; an entry naming a pattern); [6, on]: SONG -> op, rc
 *     (0 done, 1 empty / nothing to copy, 2 done without its automation, 3 playing, 4 PATTERNS FULL, 5 failed), SONG,
 *     then each track: the entry it is on (127: on no entry's pattern), the one staged (127 none), its pattern (127
 *     none), its 16 entries (pattern 1..64, 0 empty; REPS - 1)
 *   PERFORM: [op ..]: 1 STORE scene, 2 RECALL scene, 3 MUTE track (the other way; staged while playing, a staged one
 *     taken back), 4 CANCEL (all that is staged), 5 LAUNCH (0 LOOP, 1 BAR, 2 BEAT, 3 NOW) -> op, rc (RECALL: 0 done or
 *     staged, 1 empty, 2 without some automation, 4 PATTERNS FULL; LAUNCH: as UI_SET's), LAUNCH, the scene last
 *     recalled or stored (127 none), the scenes kept (16 bits, 7 a byte), then each track: muted, the mute staged (0
 *     none, 1 to unmute, 2 to mute) */
enum { ED_POOL = 78, ED_LIST, ED_PERFORM };

static int ed_song_args_ok(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (!n)
        return 1;
    if (cmd == ED_POOL)
        return n == 2u && a[0] == 1u && a[1] < NPAT;
    if (cmd == ED_LIST) {
        if (a[0] == 6u)
            return n == 2u && a[1] <= 1u;
        if (n < 3u || a[1] >= NTRK || a[2] >= NENT)
            return 0;
        if (a[0] == 1u || a[0] == 5u)
            return n == 4u && a[3] >= 1u && a[3] <= (a[0] == 1u ? NPAT : 16u);
        return n == 3u && a[0] >= 2u && a[0] <= 4u;
    }
    if (a[0] == 4u)
        return n == 1u;
    return n == 2u && ((a[0] >= 1u && a[0] <= 2u && a[1] < SCENES) || (a[0] == 3u && a[1] < NTRK) ||
                       (a[0] == 5u && a[1] < LAUNCH_N));
}

static int ed_song_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t rc = 0, k, e, r;
    if (cmd == ED_POOL) {
        if (n) {
            if (!pool_used(a[1])) rc = 1;
            else if (pool_refs(a[1])) rc = 2;
            else if (transport_busy()) rc = 3;
            else { pool_clear(a[1]); rc = pool_used(a[1]) ? 4u : 0u; }
        }
        ed_b(n ? 1u : 0u);
        ed_b(rc);
        for (k = 0; k < NPAT; k++) {
            r = pool_refs(k);
            ed_b(pool_used(k) ? pool_len(k) : 0u);
            ed_b(r > 127u ? 127u : r);
        }
        return 1;
    }
    if (cmd == ED_LIST) {
        if (n) {
            k = a[1];
            e = a[2];
            if (a[0] == 1u)
                rc = pool_used(a[3] - 1u) ? (uint32_t)song_put(k, e, a[3]) : 1u;
            else if (a[0] == 2u)
                rc = (uint32_t)song_put(k, e, SPICK_COPY);
            else if (a[0] == 3u)
                rc = (uint32_t)entry_go(k, e);
            else if (a[0] == 4u) {
                r = transport_busy();                      /* (bank_put: 2 the transport runs or a flash error, 3 */
                rc = (uint32_t)song_clear_at(k, e);        /* no flash, 4 PATTERNS FULL) */
                rc = rc == 2u ? (r ? 3u : 5u) : rc == 3u ? 5u : rc;
            } else if (a[0] == 5u) {
                if (ent_pat(k, e) == PAT_NONE) rc = 1;
                else lists.ent[k][e][1] = (uint8_t)(a[3] - 1u);
            } else
                lists.song = a[1];
            bk.gen++;
            ui.force = 1;
        }
        ed_b(n ? a[0] : 0u);
        ed_b(rc);
        ed_b(lists.song != 0u);
        for (k = 0; k < NTRK; k++) {
            r = bank_cur(k);
            ed_b(r < NENT ? r : 127u);
            r = bank_next(k);
            ed_b(r < NENT ? r : 127u);
            ed_b(trk_pid[k] && trk_pid[k] <= NPAT ? trk_pid[k] - 1u : 127u);
            for (e = 0; e < NENT; e++) {
                ed_b(lists.ent[k][e][0] <= NPAT ? lists.ent[k][e][0] : 0u);
                ed_b(lists.ent[k][e][1] & 15u);
            }
        }
        return 1;
    }
    if (cmd != ED_PERFORM)
        return 0;
    if (n) {
        if (a[0] == 1u)
            scene_saved(a[1]);
        else if (a[0] == 2u)
            rc = (uint32_t)scene_go(a[1]);
        else if (a[0] == 3u)
            mute_switch(a[1]);
        else if (a[0] == 4u)
            (void)stage_cancel();
        else {
            ui_launch = a[1];
            ui_say("LAUNCH ", LAUNCH_NAME[a[1]]);
            rc = ed_menu_save();
        }
        ui.force = 1;
    }
    ed_b(n ? a[0] : 0u);
    ed_b(rc);
    ed_b(launch_mode());
    ed_b(scene_now() < SCENES ? scene_now() : 127u);
    for (k = 0, r = 0; k < SCENES; k++)
        r |= (uint32_t)scene_used(k) << k;
    ed_b(r & 127u);
    ed_b(r >> 7 & 127u);
    ed_b(r >> 14 & 3u);
    for (k = 0; k < NTRK; k++) {
        ed_b(trk[k].p[P_MUTE] != 0);
        ed_b(mute_arm[k]);
    }
    return 1;
}
