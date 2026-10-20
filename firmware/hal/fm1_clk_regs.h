/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff) The clock tree's registers as they are, for reports: SYS_DIV, CLK_CON0, CLK_CON2, PLL_CON, PLL2_CON0,
 * PLL2_CON1, SFC_BAUD, USB_CLK (fm1_clk320.h has their fields). The SMK update loader answers them (ota.c cmd 0x7C),
 * so a loader the boot stage started from a power-on can be set against one the app started. */
#pragma once
#include <stdint.h>

static void fm1_clk_regs(uint32_t *o)            /* o: 8 words */
{
    static const uint32_t A[8] = {0x10008u, 0x1000Cu, 0x10014u, 0x119A0u, 0x119A8u, 0x119ACu, 0x40204u, 0x16A08u};
    uint32_t i;
    for (i = 0; i < 8u; i++)
        o[i] = *(const volatile uint32_t *)A[i];
}
