/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE */
/* The Bluetooth build's built-in pack (eng_sample.c SMP_PACK, editor.c ED_PACK), on the editor's host harness with the
 * pack's tables (tools/gen_samples.py FELUCCA_SMP_PACK): the flash of USR1..USR3 is host_samples (0xA0000..0xDBFFF),
 * the pack in USR2 and USR3's part. Built with -I to the pack's generated header before build/gen.
 *   ble_pack_test PACK.bin      (the pack gen_samples.py wrote with that header)
 * Checked: no pack (or a wrong one: magic, version, length, CRC, id, a data byte) -> not used, every built-in set and
 * SLICE's BREAK FUNK PIANO missing (a sine, NO SAMPLE); the install over PACK (90): BEGIN, WRITE, END, the header last,
 * then the pack in use, its zones and SLICE's tables the pack's, reads from the pack; refused while the transport runs;
 * confined to the pack's range; a power cut before END leaves no pack; END refuses another pack and bad data; one user
 * slot (USR2 / USR3: skipped, never erasing the pack); with the pack every default sound has its samples. */
#define EDITOR_TEST_NO_MAIN 1
#define SMP_PACK_XIP() ((const uint8_t *)&host_samples[1][0])
#include "editor_test.c"
#ifndef SMP_PACK
#error "build with the pack's generated header (tools/gen_samples.py FELUCCA_SMP_PACK) first in the include path"
#endif

static uint8_t pk[SMP_PACK_SIZE];                       /* the pack file */
static uint32_t pk_n;
static uint8_t *pack_flash(void) { return &host_samples[1][0]; }
static int pack_same(void)                               /* the flash holds the file: its header's 20 bytes, the payload */
{
    return !memcmp(pack_flash(), pk, 20) && !memcmp(pack_flash() + SMP_PACK_HDR, pk + SMP_PACK_HDR, SMP_PACK_LEN);
}

static uint32_t pack_req(const uint8_t *a, uint32_t n)  /* -> rc (the reply's last byte), 99: no reply */
{
    uint32_t got = request(ED_PACK, a, n);
    return got >= 7u && host_wire[4] == ED_PACK ? host_wire[got - 2u] : 99u;
}
static uint32_t pack_begin(void) { uint8_t a[1] = {1}; return pack_req(a, 1); }
static uint32_t pack_write(uint32_t off, const uint8_t *p, uint32_t n)
{
    static uint8_t a[4 + 300];
    a[0] = 2;
    a[1] = (uint8_t)(off & 127u);
    a[2] = (uint8_t)(off >> 7 & 127u);
    a[3] = (uint8_t)(off >> 14 & 127u);
    return pack_req(a, 4u + pack7(p, n, a + 4));
}
static uint32_t pack_end(const uint8_t *hdr)
{
    static uint8_t a[1 + 32];
    a[0] = 3;
    return pack_req(a, 1u + pack7(hdr, 20, a + 1));
}
static uint32_t pack_data(uint32_t upto)                /* every WRITE up to (payload) byte upto; -> the first rc not 0 */
{
    uint32_t off, rc;
    for (off = SMP_PACK_HDR; off < SMP_PACK_HDR + upto; off += 256u) {
        uint32_t n = SMP_PACK_HDR + SMP_PACK_LEN - off < 256u ? SMP_PACK_HDR + SMP_PACK_LEN - off : 256u;
        if ((rc = pack_write(off, pk + off, n)) != 0u)
            return rc;
    }
    return 0;
}
static uint32_t install(void)                            /* BEGIN, every WRITE, END: -> END's rc (or the first other) */
{
    uint32_t rc = pack_begin();
    if (rc)
        return 100u + rc;
    if ((rc = pack_data(SMP_PACK_LEN)) != 0u)
        return 200u + rc;
    return pack_end(pk);
}
static int all_missing(void)
{
    uint32_t si;
    int m = 1;
    for (si = 0; si < SMP_NSETS; si++)
        m &= smp_set_missing(si);
    return m && !slc_get(0) && !slc_get(SLC_SRC_PIANO) && !slc_get(SLC_SRC_FUNK);
}

int main(int argc, char **argv)
{
    int bad = 0, ok;
    uint32_t i, k, e0;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : 0;
    if (!f || !(pk_n = (uint32_t)fread(pk, 1, sizeof pk, f)) || pk_n != SMP_PACK_HDR + SMP_PACK_LEN) {
        printf("ble_pack: the pack file (argument) of this header's build\n");
        return 1;
    }
    fclose(f);
    reset();
    memset(host_samples, 0xFF, sizeof host_samples);
    smp_pack_scan();
    bad += check("pack: none in flash -> state 0, not in use", smp_pack_state() == 0u && !smp_pack_ok);
    bad += check("pack: none -> every built-in set and SLICE's BREAK, PIANO, FUNK missing (a sine, NO SAMPLE)",
                 all_missing());
    bad += check("pack: one user slot, USR1 (USR2 / USR3 are the pack's flash)",
                 SMP_USER_HAS(0) && !SMP_USER_HAS(1) && !SMP_USER_HAS(2) && SMP_SHOWN == 1 &&
                 SMP_PACK_BASE == 0xB4000u && SMP_PACK_BASE + SMP_PACK_SIZE == 0xDC000u);
    {   /* INFO: none, this firmware's length and id */
        uint8_t a[1] = {0};
        uint32_t n = request(ED_PACK, a, 1), len = 0, id = 0;
        for (i = 0; i < 3u; i++) len |= (uint32_t)host_wire[7 + i] << (7u * i);
        for (i = 0; i < 5u; i++) id |= (uint32_t)host_wire[10 + i] << (7u * i);
        bad += check("PACK INFO: state 0, the pack's length and id as built", n == 17u && host_wire[5] == 0 &&
                     host_wire[6] == 0 && len == SMP_PACK_LEN && id == SMP_PACK_ID && host_wire[15] == 0);
    }

    memset(host_samples[0], 0x5A, sizeof host_samples[0]);   /* USR1's flash: never touched by the pack */
    song.playing = 1;
    k = host_erases;
    bad += check("PACK refused while the transport runs (rc 3), nothing erased",
                 pack_begin() == 3u && pack_write(256, pk + 256, 256) == 3u && pack_end(pk) == 3u && host_erases == k);
    song.playing = 0;
    k = host_erases;
    bad += check("PACK install: BEGIN, WRITE, END (header last) -> rc 0, the pack in use", install() == 0u &&
                 smp_pack_ok && smp_pack_state() == 1u);
    bad += check("PACK install: the flash holds the file's header and payload, byte for byte", pack_same());
    for (ok = 1, i = 0; i < sizeof host_samples[0]; i++)
        ok &= host_samples[0][i] == 0x5A;
    bad += check("PACK install: USR1's flash untouched; erases only in the pack's sectors",
                 ok && host_erases - k == (SMP_PACK_HDR + SMP_PACK_LEN + 4095u) / 4096u);
    bad += check("pack in use: SMP_DATA is the pack's data, SMP_ZONES its zone table",
                 SMP_DATA == pack_flash() + SMP_PACK_HDR &&
                 !memcmp(SMP_ZONES, pk + SMP_PACK_HDR + SMP_PACK_ZONES, sizeof SMP_ZONES));
    ok = 1;
    for (i = 0; i < SMP_NSETS; i++)
        ok &= !smp_set_missing(i);
    bad += check("pack in use: every built-in set has its samples", ok);
#ifdef SMP_PACK_SLC_BREAK
    bad += check("pack in use: SLICE's BREAK, FUNK, PIANO are the pack's tables",
                 slc_get(0) == (const slc_src_t *)(const void *)(SMP_DATA + SMP_PACK_SLC_BREAK) && slc_get(0)->len == 44100u &&
                 slc_get(SLC_SRC_FUNK) && slc_get(SLC_SRC_FUNK)->len && slc_get(SLC_SRC_PIANO) && slc_get(SLC_SRC_PIANO)->len);
#endif
    {   /* the reads come from the pack: a built-in zone decoded, then one data byte changed under it */
        const smp_zone_t *z = &SMP_ZONES[0];
        voice_t v;
        int32_t a1[8], a2[8];
        memset(&v, 0, sizeof v);
        for (i = 0; i < 8u; i++) a1[i] = sample_next(z, &v, 0);
        host_samples[1][SMP_PACK_HDR + z->off] ^= 0x77u;
        memset(&v, 0, sizeof v);
        for (i = 0; i < 8u; i++) a2[i] = sample_next(z, &v, 0);
        host_samples[1][SMP_PACK_HDR + z->off] ^= 0x77u;
        bad += check("pack in use: a built-in sound is decoded from the pack's flash", memcmp(a1, a2, sizeof a1) != 0);
    }
    for (k = 0; k < NTRK; k++) {                         /* power-on: every track's default sound */
        uint32_t code = 0;
        ok = !snd_missing(&trk[k], &code);
        if (!ok) break;
    }
    bad += check("pack in use: no default sound is missing its samples", ok);
    {   /* SAMPLE's every factory preset and SLICE's: with the pack, none missing */
        track_t *t = &trk[0];
        ok = 1;
        set_engine_of(t, ENGI_SAMPLE);
        for (i = 0; i < ENGINES[ENGI_SAMPLE]->npresets; i++) {
            uint32_t code = 0;
            apply_preset_to(t, i);
            e0 = (uint32_t)t->p[P_E0] % SMP_NALL;
            ok &= e0 >= SMP_NSETS || !snd_missing(t, &code);
        }
#if FELUCCA_SLICE
        set_engine_of(t, ENGI_SLICE);
        for (i = 0; i < ENGINES[ENGI_SLICE]->npresets; i++) {
            uint32_t code = 0;
            apply_preset_to(t, i);
            ok &= !snd_missing(t, &code);
        }
#endif
        bad += check("pack in use: every SAMPLE and SLICE factory sound on a built-in source has its samples", ok);
        reset();
    }
    {   /* each field of the header, and a data byte: not this firmware's pack -> not used */
        static const uint32_t FIELD[5] = {0, 4, 8, 12, 16};
        ok = 1;
        for (i = 0; i < 6u; i++) {
            uint8_t *b = i < 5u ? pack_flash() + FIELD[i] : pack_flash() + SMP_PACK_HDR + 1000u;
            *b ^= 1u;
            smp_pack_scan();
            ok &= smp_pack_state() == (i ? 2u : 0u) && !smp_pack_ok && all_missing();
            *b ^= 1u;
        }
        smp_pack_scan();
        bad += check("pack: a wrong magic (none), version, length, CRC, id or data byte -> not used", ok && smp_pack_ok);
    }

    bad += check("PACK WRITE before BEGIN (the pack in use) refused (rc 1)", pack_write(256, pk + 256, 256) == 1u);
    k = host_erases;
    bad += check("PACK BEGIN: the pack no longer in use, its header erased (a sounding voice ends)",
                 pack_begin() == 0u && !smp_pack_ok && all_missing() && host_erases - k == 1u &&
                 pack_flash()[0] == 0xFFu && SMP_ZONES[0].n == 0u);
    {   /* the range: below the payload, unaligned, past its end, past the pack */
        uint8_t z[256] = {0};
        ok = pack_write(0, z, 256) == 1u && pack_write(512 + 16, z, 16) == 1u &&
             pack_write(SMP_PACK_HDR + ((SMP_PACK_LEN + 255u) & ~255u), z, 256) == 1u &&
             pack_write(SMP_PACK_SIZE, z, 256) == 1u;
        bad += check("PACK WRITE confined to the pack's payload (rc 1 outside it, unaligned)", ok);
    }
    k = pack_data(SMP_PACK_LEN / 2u);                   /* a power cut half way: no END */
    smp_pack_scan();
    bad += check("power cut during the install (no END): no header, the pack not used",
                 !k && smp_pack_state() == 0u && !smp_pack_ok && all_missing());
    bad += check("... the header's bytes still erased while the data comes (header last)",
                 pack_flash()[0] == 0xFFu && pack_flash()[19] == 0xFFu);
    {
        uint8_t h[20];
        memcpy(h, pk, 20);
        h[16] ^= 1u;                                    /* another id */
        ok = pack_begin() == 0u && pack_data(SMP_PACK_LEN) == 0u && pack_end(h) == 2u && !smp_pack_ok &&
             pack_flash()[0] == 0xFFu;
        h[16] ^= 1u;
        h[8] ^= 1u;                                     /* another length */
        ok &= pack_end(h) == 2u && !smp_pack_ok;
        h[8] ^= 1u;
        host_samples[1][SMP_PACK_HDR + 77u] ^= 0x10u;   /* the data damaged in flash */
        ok &= pack_end(h) == 5u && !smp_pack_ok && pack_flash()[0] == 0xFFu;
        host_samples[1][SMP_PACK_HDR + 77u] ^= 0x10u;
        ok &= pack_end(h) == 0u && smp_pack_ok;
        bad += check("PACK END: another pack's header (rc 2), damaged data (rc 5): no header written; then 0", ok);
    }
    {   /* USR2 / USR3: a backup's restore skips them, never erasing the pack */
        uint8_t a[600];
        uint32_t n;
        a[0] = 1;
        request(ED_SMP_ERASE, a, 1);
        ok = host_wire[5] == 1 && host_wire[6] == 0 && smp_pack_ok && smp_pack_state() == 1u;
        a[0] = 2;
        request(ED_SMP_BEGIN, a, 1);
        ok &= host_wire[6] == 0 && smp_pack_ok;
        a[1] = 0; a[2] = 4; a[3] = 0;                   /* off 512 */
        n = pack7(pk + 512, 256, a + 4);
        request(ED_SMP_WRITE, a, 4 + n);
        ok &= host_wire[9] == 0 && smp_pack_state() == 1u;
        n = pack7(pk, 480, a + 1);
        request(ED_SMP_END, a, 1 + n);
        ok &= host_wire[6] == 0 && smp_pack_state() == 1u && pack_same();
        bad += check("USR2 / USR3 (a backup's restore): ERASE, BEGIN, WRITE, END skipped (rc 0), the pack intact", ok);
        request(ED_SMP_INFO, a, 0);
        bad += check("SMP_INFO offers one slot, USR1", host_wire[5] == 1);
    }
    printf("%s\n", bad ? "BLE PACK TEST FAILED" : "ble_pack: all checks passed");
    return bad != 0;
}
