/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE */
/* The plain build (no FELUCCA_BLE) after the Bluetooth build set CLOCK to BLE (G_CLOCK 3, a value the plain build has
 * no name for) or LINK (4). Skiff: CLOCK is the unit's (ui.c ui_unit_g, a settings byte; a project still stores it and
 * a load leaves the unit's as it is): at power-on the stored byte is clamped to the plain build's last source (TRS),
 * never past the names; a project stored with BLE or LINK loads, the unit's CLOCK unchanged. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#if FELUCCA_BLE
#error "the plain build's test: build without FELUCCA_BLE"
#endif
static project_t pj;
int main(void)
{
    int bad = 0;
    uint32_t v;
    for (v = 3; v <= 4u; v++) {
        ui_power_on();
        unit_g_adopt = 0;
        ui_unit_g(0) = (uint8_t)v;                    /* (as the Bluetooth build saved CLOCK: UNIT_G[0]) */
        unit_g_boot();
        bad += check(v == 3u ? "the unit's CLOCK BLE reads as TRS in the plain build" :
                               "the unit's CLOCK LINK reads as TRS in the plain build",
                     UNIT_G[0] == G_CLOCK && song.g[G_CLOCK] == GP[G_CLOCK].max && GP[G_CLOCK].max == 2 &&
                     str_eq(GP[G_CLOCK].names[song.g[G_CLOCK]], "TRS"));
        song.g[G_CLOCK] = 1;
        project_capture(&pj);
        pj.g[G_CLOCK] = (int16_t)v;
        pj.sum = proj_sum(&pj);                       /* (as the Bluetooth build stored it) */
        bad += check(v == 3u ? "a project with CLOCK BLE loads in the plain build, the unit's CLOCK kept" :
                               "a project with CLOCK LINK loads in the plain build, the unit's CLOCK kept",
                     project_restore_runtime(&pj) == 0 && song.g[G_CLOCK] == 1);
    }
    printf(bad ? "clk_ble_plain: FAILED\n" : "clk_ble_plain: all checks passed\n");
    return bad != 0;
}
