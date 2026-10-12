#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The README's pictures from the firmware's own screen renders (tests/ui_render.c: build/ui_new/ppm), pixel for pixel
at twice their size: docs/img/<screen>.png, and docs/img/themes.png (HOME in every THEME, 3 x 2).

  tools/gen_readme_shots.py build/ui_new/ppm
"""
import sys
from pathlib import Path

from PIL import Image

SHOTS = ["step_tiles", "roll_chords", "drum", "chance_flam", "auto_list", "layer_seq_tools",
         "song_playing", "pool", "layer_perform_scenes", "layer_glo_active",
         "env", "lfo_dest", "dly", "reverb_spring", "global_swing", "voice_glide", "edit_analog", "edit_fm6",
         "dx_sound", "slices_break", "edit_phys", "chord", "fx_duck", "menu", "splash"]
THEMES = ["SKIFF", "HI-CON", "PAPER", "CRYPT", "NEON", "QUARK"]


def main(ppm):
    ppm, out = Path(ppm), Path(__file__).resolve().parents[1] / "docs" / "img"
    out.mkdir(parents=True, exist_ok=True)
    big = lambda name: Image.open(ppm / f"{name}.ppm").convert("RGB").resize((480, 480), Image.NEAREST)
    for s in SHOTS:
        big(f"SKIFF_{s}").save(out / f"{s}.png", optimize=True)
    gap, sheet = 24, Image.new("RGBA", (3 * 480 + 2 * 24, 2 * 480 + 24), (0, 0, 0, 0))
    for i, t in enumerate(THEMES):                   # (a transparent gap: the README's page shows through)
        sheet.paste(big(f"{t}_six_home"), ((i % 3) * (480 + gap), (i // 3) * (480 + gap)))
    sheet.save(out / "themes.png", optimize=True)
    print(f"wrote {len(SHOTS)} screens and themes.png to {out}")


if __name__ == "__main__":
    main(sys.argv[1])
