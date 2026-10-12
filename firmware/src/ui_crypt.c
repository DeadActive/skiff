/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff C3) THEME CRYPT's widgets (design/mock_crypt.py, B TORCHLIT, the user's pick): stone where BLOCKS lays a flat
 * tile, the ASCII double frame, and its sprites, our own pixel drawings: the skull (a muted track's crossed), the
 * torch, a sword's hilt (the cards' gauges are swords), the blood drop (REC). Drawn on the canvas (cv_*) when
 * ux.crypt; with any other THEME BLOCKS draws as before. Included by ui_blocks.c. */

#define CR_GOLD RGB(255, 214, 96)                    /* a blade's edge, the hot card's frame */
#define CR_FLAME RGB(255, 238, 150)
#define CR_EMBER RGB(232, 96, 36)
#define CR_BLOOD RGB(196, 40, 32)

/* (the stone tile, cr_stone: gfx.c, under every SURF cell) */

/* the ASCII double line: two frames 2 px apart */
static void cr_frame2(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_frame(x, y, w, h, c);
    cv_frame(x + 2, y + 2, w - 4, h - 4, c);
}

/* a sprite: rows of characters, '#' c1, '.' c2 (0: left as it is), a space nothing */
static void cr_sprite(int32_t x, int32_t y, const char *const *rows, uint32_t n, uint16_t c1, uint16_t c2)
{
    uint32_t j, i;
    for (j = 0; j < n; j++)
        for (i = 0; rows[j][i]; i++)
            if (rows[j][i] == '#' || (rows[j][i] == '.' && c2))
                cv_pset(x + (int32_t)i, y + (int32_t)j, rows[j][i] == '#' ? c1 : c2);
}

static const char *const CR_SKULL[9] = {"  #####  ", " ####### ", "#########", "#..###..#", "#..###..#", "####.####",
                                        " ####### ", "  #.#.#  ", "  #####  "};
static const char *const CR_SKULL_X[9] = {"  #####  ", " ####### ", "#########", "#.#####.#", "##.###.##",
                                          "####.####", " ####### ", "  #.#.#  ", "  #####  "};   /* muted: crossed eyes */
static void cr_skull(int32_t x, int32_t y, uint16_t c, uint16_t eyes, int muted)
{
    cr_sprite(x, y, muted ? CR_SKULL_X : CR_SKULL, 9, c, eyes);
}

/* the torch: the flame (two frames, flickering), its head, the stick; 5 x 12, z times that (2 and more: in a 1 px dark
 * outline, so it shows on a band of its own amber: HOME's selected track) */
static void cr_torch(int32_t x, int32_t y, uint32_t frame, int32_t z)
{
    static const char *const T[2][12] = {
        {"  Y  ", " YYY ", " YFY ", "YFFFY", "YORFY", " ORO ", " BBB ", "  B  ", "  B  ", " BBB ", "  B  ", "  B  "},
        {"   Y ", "  YY ", " YYY ", "YFFFY", "YOFRY", " ORO ", " BBB ", "  B  ", "  B  ", " BBB ", "  B  ", "  B  "}};
    uint32_t j, i;
    for (j = 0; z > 1 && j < 12u; j++)
        for (i = 0; i < 5u; i++)
            if (T[frame & 1u][j][i] != ' ')
                cv_rect(x + (int32_t)i * z - 1, y + (int32_t)j * z - 1, z + 2, z + 2, T_BG);
    for (j = 0; j < 12u; j++)
        for (i = 0; i < 5u; i++) {
            char k = T[frame & 1u][j][i];
            if (k != ' ')
                cv_rect(x + (int32_t)i * z, y + (int32_t)j * z, z, z, k == 'Y' ? CR_GOLD : k == 'F' ? CR_FLAME :
                        k == 'O' ? T_THEME : k == 'R' ? CR_EMBER : T_DIM);
        }
}

/* a gauge as a sword (the card's): the hilt at the left, the blade as long as the value along a groove, its edge
 * brighter, a point at its end; 5 rows from y, w px */
static void cr_sword(int32_t x, int32_t y, int32_t w, int32_t ratio, uint16_t blade, uint16_t edge, uint16_t hilt,
                     uint16_t groove)
{
    static const char *const H[5] = {"  #       ", " ### ##   ", "########  ", " ### ##   ", "  #       "};
    int32_t len = (w - 10) * (ratio > 1000 ? 1000 : ratio) / 1000;
    cv_rect(x + 10, y + 2, w - 10, 1, groove);
    if (len > 0) {
        cv_rect(x + 9, y + 1, len, 3, blade);
        cv_rect(x + 9, y + 1, len, 1, edge);
        cv_pset(x + 9 + len, y + 2, blade);
    }
    cr_sprite(x, y, H, 5, hilt, 0);
}

/* a level as a torch (MIXER, FX, LANES): a 6 px column fh tall up to y1, ember at its foot, amber, gold at its top, its
 * flame on it (out: dim, no flame, a bar where it ends) */
static void cr_meter(int32_t x, int32_t y1, int32_t fh, int out)
{
    static const char *const FL[6] = {"  ##  ", " #..# ", " #..# ", "#....#", "#....#", " #### "};
    int32_t r;
    for (r = 0; r < fh; r++)
        cv_rect(x, y1 - 1 - r, 6, 1, out ? T_DIM : r * 3 < fh ? CR_EMBER : r * 3 < 2 * fh ? T_THEME : CR_GOLD);
    if (!out && fh)
        cr_sprite(x, y1 - fh - 6, FL, 6, T_THEME, CR_FLAME);
    else
        cv_rect(x - 3, y1 - fh - 1, 12, 2, T_DIM);
}

/* a note as a sword (the piano roll): its crossguard gold at x across the row (h + 2), the blade as long as the note,
 * its edge bright, a point at its end */
static void cr_blade(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y - 1, 1, h + 2, CR_GOLD);
    if (w > 2) {
        cv_rect(x + 1, y, w - 2, h, c);
        cv_rect(x + 1, y, w - 2, 1, ux_mix(c, T_TEXT, 50));
        cv_rect(x + w - 1, y + h / 2, 1, 1, c);
    }
}

/* a menu row's dotted leader, as an old book's contents: a dot every 3 px from x0 to x1 */
static void cr_leader(int32_t x0, int32_t x1, int32_t y)
{
    for (; x0 < x1; x0 += 3)
        cv_rect(x0, y, 1, 1, T_DIM);
}

/* REC: a blood drop (the selected track armed: full; another: its outline); 7 x 9 */
static void cr_drop(int32_t x, int32_t y, int full)
{
    static const char *const D[9] = {"   #   ", "  ###  ", "  ###  ", " ##### ", "#######", "#######", "#######",
                                     " ##### ", "  ###  "};
    static const char *const O[9] = {"   #   ", "  # #  ", "  # #  ", " #   # ", "#     #", "#     #", "#     #",
                                     " #   # ", "  ###  "};
    cr_sprite(x, y, full ? D : O, 9, T_REC, 0);
}

/* ---------------------------------------------------------------- (Skiff C4) the graphs as ASCII art */
/* a 2 px stroke from (xa, ya) to (xb, yb): 2 x 2 blocks along it */
static void cr_stroke(int32_t xa, int32_t ya, int32_t xb, int32_t yb, uint16_t c)
{
    int32_t dx = xb - xa, dy = yb - ya, n = dx < 0 ? -dx : dx, m = (dy < 0 ? -dy : dy) / 2, k;
    if (m > n)
        n = m;
    if (!n)
        n = 1;
    for (k = 0; k <= n; k++)
        cv_rect(xa + dx * k / n, ya + dy * k / n, 2, 2, c);
}

/* a curve as ASCII art (design/mock_u5.py's): a level a character column (lv[i]: the grid line from the top, cells
 * cw x ch from (x0, y0)), joined cell by cell: _ where it holds, / or \ where it moves a line or two, _| or |_ where it
 * jumps (a saw's or a square's edge) */
static void cr_art(int32_t x0, int32_t y0, int32_t cw, int32_t ch, const uint8_t *lv, uint32_t n, uint16_t c)
{
    uint32_t i;
    for (i = 0; i + 1u < n; i++) {
        int32_t r = lv[i], r2 = lv[i + 1u], xa = x0 + (int32_t)i * cw, ya = y0 + r * ch, yb = y0 + r2 * ch;
        if (r2 - r >= 3 || r - r2 >= 3) {
            cr_stroke(xa, ya, xa + cw - 2, ya, c);
            cr_stroke(xa + cw - 2, ya, xa + cw - 2, yb, c);
        } else {
            cr_stroke(xa, ya, xa + cw, yb, c);
        }
    }
}

/* the ASCII shade under such a curve: a ':' a cell (two dots), down to the line `floor` */
static void cr_dots(int32_t x0, int32_t y0, int32_t cw, int32_t ch, const uint8_t *lv, uint32_t n, int32_t floor,
                    uint16_t c)
{
    uint32_t i;
    int32_t k;
    for (i = 0; i + 1u < n; i++)
        for (k = (lv[i] > lv[i + 1u] ? lv[i] : lv[i + 1u]) + 1; k < floor; k++) {
            cv_rect(x0 + (int32_t)i * cw + cw / 2 - 1, y0 + k * ch + ch / 4, 1, 1, c);
            cv_rect(x0 + (int32_t)i * cw + cw / 2 - 1, y0 + k * ch + ch * 3 / 4, 1, 1, c);
        }
}

/* the snake's head (LFO's motif), its snout at (x, y): an eye, a forked tongue */
static void cr_head(int32_t x, int32_t y, uint16_t c)
{
    static const char *const H[6] = {"  ####  ", " ###### ", "########", "##.#####", "########", " ###### "};
    cr_sprite(x - 2, y - 3, H, 6, c, T_INK);
    cv_rect(x + 6, y, 4, 1, CR_BLOOD);
    cv_pset(x + 10, y - 1, CR_BLOOD);
    cv_pset(x + 10, y + 1, CR_BLOOD);
}

/* a curve as a snake: the strokes doubled for a body (its belly darker), gold scales along it, the head at its end */
static void cr_snake(int32_t x0, int32_t y0, int32_t cw, int32_t ch, const uint8_t *lv, uint32_t n, uint16_t c,
                     uint16_t belly)
{
    uint32_t i;
    if (n < 2u)
        return;
    cr_art(x0, y0 + 2, cw, ch, lv, n, belly);
    cr_art(x0, y0, cw, ch, lv, n, c);
    for (i = 1; i + 1u < n; i += 2u)
        cv_rect(x0 + (int32_t)i * cw + cw / 2 - 1, y0 + (lv[i] + lv[i + 1u]) * ch / 2, 2, 1, CR_GOLD);
    cr_head(x0 + (int32_t)(n - 1u) * cw, y0 + lv[n - 1u] * ch + 1, c);
}

/* a character of the theme's face (CRYPT's pixel S or V) on a baseline */
static void cr_ch(int32_t x, int32_t base, const aafont_t *f, char ch, uint16_t c)
{
    char s[2] = {ch, 0};
    cv_text(x, base - f->asc, f, s, c);
}

/* a dagger, pointing down (FLT's cutoff), 10 x 14 */
static void cr_dagger(int32_t x, int32_t y, uint16_t c)
{
    static const char *const D[7] = {"  #  ", "  #  ", "#####", "  #  ", "  #  ", "  #  ", "  #  "};
    uint32_t j, i;
    for (j = 0; j < 7u; j++)
        for (i = 0; i < 5u; i++)
            if (D[j][i] == '#')
                cv_rect(x + 2 * (int32_t)i, y + 2 * (int32_t)j, 2, 2, c);
}

/* a sprite drawn z times its size */
static void cr_spritez(int32_t x, int32_t y, const char *const *rows, uint32_t n, uint16_t c1, uint16_t c2, int32_t z)
{
    uint32_t j, i;
    for (j = 0; j < n; j++)
        for (i = 0; rows[j][i]; i++)
            if (rows[j][i] == '#' || (rows[j][i] == '.' && c2))
                cv_rect(x + (int32_t)i * z, y + (int32_t)j * z, z, z, rows[j][i] == '#' ? c1 : c2);
}

/* half the pixels of a box in c (a checkerboard: the ASCII shade, blood in a saw's teeth) */
static void cr_dither(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (((i + j) & 1) == 0)
                cv_pset(i, j, c);
}
