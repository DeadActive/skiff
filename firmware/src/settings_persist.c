/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared PER4 layout: each feature updates only its own fields, preserving
 * the other feature's saved preferences when either is built independently.
 * PER1/PER2 are upstream; PER3 added bold; PER4 added favorites.
 * palette: UI_PAL_TAG + index; an old id (below 20, earlier firmware) is migrated on import.
 * bold: no longer used (one font weight); kept as it was saved, unless it holds the HOLD setting (panel.c).
 * zoom: no longer used (the large readout); kept as it was saved, unless it holds the LEDS setting (panel.c).
 * favorites.factory[15][28..31]: CLICK / CLICK LEVEL / COUNT-IN (1.1), STYLE (retired, Skiff U2: left as saved), MENU's flags, the layers seen (bytes no
 * engine uses; 0 in older settings = every one's default); [15][27]: SCREEN OFF (1.1.5; 0 = 30 MIN, the default);
 * [15][26]: Skiff's LAUNCH (ui.c ui_launch; 0 = LOOP, an unknown value read as LOOP); [15][25]: Skiff's PERFORM KEYS
 * (ui.c ui_perf_keys: 0 TRACK, 1 SCENE); [15][24]: bank.c SCENE_FREED; [15][23]: (Skiff U1) ui.c ui_set_rev, the
 * one-time changes done; [15][21]: (Skiff phase 9) PRESETS' LIST type (ui.c ui_ptag; 0 = none, an unknown value
 * read as none). ([14][0] .. [15][17] held Skiff's scenes before phase 5; row 14 is DRUM-X's since.)
 * MENU's flags (ui.c PREF_*) are append-only bits whose 0 is the default: BPM LOCK (16) is clear in every older setting
 * = unlocked, LARGE (32) = OFF, RESTORE LAST OFF (128, 1.2) = ON, read as saved (nothing to migrate, so importing twice changes nothing). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
    uint32_t bold;
    struct { uint8_t factory[16][32]; uint32_t user, filter; } favorites;
} persist_t;
#define PERSIST_MAGIC 0x50455234u

/* Normalize in place; 1 = current, 2 = migrated, 0 = invalid. */
#if FELUCCA_BLE
static uint8_t bt_mac_sum(const uint8_t *r);        /* bt_devs.c: the FM-1's own Bluetooth address' check */
#endif
static int settings_import(persist_t *p, int n)
{
    int current = n == (int)sizeof *p && p->magic == PERSIST_MAGIC;
    int old3 = n == (int)(sizeof *p - sizeof p->favorites) && p->magic == 0x50455233u;
    int old2 = n == (int)(sizeof *p - sizeof p->favorites - sizeof p->bold) && p->magic == 0x50455232u;
    int old1 = n == (int)(8u + sizeof(panel_t)) && p->magic == 0x50455231u;
    if (!(current || old3 || old2 || old1)) return 0;
    if (old1) {
        panel_t old;
        memcpy(&old, (uint8_t *)p + 8, sizeof old);
        p->panel = old;
        p->lowcut = p->zoom = 0;
    }
    if (old1 || old2) p->bold = 0;
    if (!current) memset(&p->favorites, 0, sizeof p->favorites);
    {   /* CLICK, CLICK LEVEL, COUNT-IN (ui.c ui_rec_prefs): two bits each, 3 is no value: the default (0) */
        uint32_t b = p->favorites.factory[15][28], f;
        for (f = 0; f < 3u; f++)
            if (((b >> (2u * f)) & 3u) == 3u)
                b &= ~(3u << (2u * f));
        p->favorites.factory[15][28] = (uint8_t)b;
    }
    if ((p->favorites.factory[15][27] ^ 3u) > 4u)  /* SCREEN OFF (1.1.5, ui.c ui_scr): stored ^ 3, unknown = 0 (30 MIN) */
        p->favorites.factory[15][27] = 0;
    if (p->favorites.factory[15][21] >= 8u)        /* (Skiff phase 9) PRESETS' LIST type (ui.c ui_ptag, below PT_N) */
        p->favorites.factory[15][21] = 0;
#if BOARD_SMK37
    if (p->favorites.factory[15][12] >= SMK_KV_N)  /* (SMK-37) KEY VELOCITY (ui.c SMK_KV_BYTE): unknown = DYNAMIC */
        p->favorites.factory[15][12] = 0;
#endif
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(palette_from_stored(p->palette));
    settings.magic = SETTINGS_MAGIC;
    settings.palette = palette_from_stored(p->palette);
    settings.lowcut = p->lowcut;
    settings.zoom = p->zoom;
    settings_hold = (uint8_t)hold_from_stored(p->bold);
    settings_leds = (uint8_t)leds_from_stored(p->zoom);
#ifdef FELUCCA_FAVORITES
#if FELUCCA_BLE
    {   /* (BLE MIDI: 2026 DEADACTIVE) a backup from another unit (the editor's restore) leaves this FM-1's own Bluetooth
         * bytes as they are: its address (bt_devs.c bt_mac) and its radio calibration's copy (bt_rf_io, rows 9..11
         * [8..31]); at power-on (nothing of this unit's in RAM yet) the stored ones load */
        int mine = favorites.factory[13][22] == 0xA5u && favorites.factory[13][23] == bt_mac_sum(&favorites.factory[13][16]);
        uint32_t r;
        if (mine)
            memcpy(&p->favorites.factory[13][16], &favorites.factory[13][16], 8);
        if (mine || favorites.factory[11][31] == 0xA5u)
            for (r = 9; r <= 11u; r++)
                memcpy(&p->favorites.factory[r][8], &favorites.factory[r][8], 24);
    }
#endif
    memcpy(&favorites, &p->favorites, sizeof favorites);
    favorites.filter = favorites.filter == 1u;
#if defined(FM4_NPRESETS) && !FELUCCA_FM4
    {   /* DIGITAL's starred presets (engine 1, retired) -> the FM6 presets that cover them (fm4_convert.c) */
        uint32_t k;
        for (k = 0; k < FM4_NPRESETS; k++)
            if ((favorites.factory[ENGI_DIGITAL][0] >> k) & 1u)
                favorites.factory[ENGI_FM6][FM4_TO_FM6[k] / 8u] |= (uint8_t)(1u << (FM4_TO_FM6[k] % 8u));
        favorites.factory[ENGI_DIGITAL][0] = 0;  /* ((1.3) bytes 2..31: MIDI LEARN's map, midi_control.c ML_N) */
    }
#endif
#ifdef SMP_SET_PERC
    if ((favorites.factory[ENGI_SAMPLE][0] >> SMP_SET_PERC) & 1u) {   /* SAMPLE's PERC (preset 4, retired): DRUM's */
        favorites.factory[ENGI_DRUM][0] |= 1u;                         /* kit (core.h drum_from_perc) */
        favorites.factory[ENGI_SAMPLE][0] &= (uint8_t)~(1u << SMP_SET_PERC);
    }
#endif
#endif
    if (p->panel.magic == PANEL_MAGIC) panel = p->panel;
    return current ? 1 : 2;
}

/* Start with the last imported/saved object, including fields owned by a
 * feature absent from this build. No save-on-boot or extra flash writes. */
static void settings_export(persist_t *p)
{
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(settings.palette);
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom = leds_to_stored(settings.zoom, settings_leds);
    p->panel = panel;
    p->bold = hold_to_stored(p->bold, settings_hold);
#ifdef FELUCCA_FAVORITES
    memcpy(&p->favorites, &favorites, sizeof favorites);
#endif
}
