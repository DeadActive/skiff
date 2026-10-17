#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The README's pictures from the firmware's own screen renders (tests/ui_render.c: build/ui_new/ppm), pixel for pixel
at twice their size: docs/img/<screen>.png, docs/img/themes.png (HOME in every THEME, 3 x 2), docs/img/fm6_themes.png (FM6's
OPERATOR in CRYPT, NEON and QUARK), docs/img/new_1_2.png (1.2's release: a type's list, DRIVE, the FX layer held),
docs/img/new_1_3.png (1.3's: the INSERT, LFO 2, MIDI LEARN) and
docs/skiff.png (the README's first picture: HOME, STEP, PERFORM, the MIXER, DELAY, SHIFT's map; 3 x 2 on the dark ground).

  tools/gen_readme_shots.py build/ui_new/ppm
"""
import sys
from pathlib import Path

from PIL import Image

SHOTS = ["step_tiles", "roll_chords", "drum", "chance_flam", "auto_list", "layer_seq_tools",
         "song_playing", "pool", "layer_perform_scenes", "layer_glo_active",
         "env", "lfo_dest", "dly", "reverb_spring", "global_swing", "voice_glide", "edit_analog", "edit_fm6",
         "dx_sound", "slices_break", "edit_phys", "chord", "fx_duck", "menu", "splash",
         "fm_op_tone", "fm_op_env", "fm_op_keys", "fm_all_lfo",
         "presets_bass", "drive_fold", "env_2", "lanes_tune", "chorus_flanger", "six_fx", "perform_held",   # (phase 9)
         "insert_flanger", "lfo_2", "menu_midi", "learn_cc"]   # (1.3)
THEMES = ["SKIFF", "HI-CON", "PAPER", "CRYPT", "NEON", "QUARK"]
HERO = ["home", "step_tiles", "layer_perform", "six_mixer", "dly", "shift_env"]   # docs/skiff.png, as 1.0's


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
    fm6 = Image.new("RGBA", (3 * 480 + 2 * 24, 480), (0, 0, 0, 0))   # (1.1) OPERATOR in the three themes of their own
    for i, t in enumerate(["CRYPT", "NEON", "QUARK"]):
        fm6.paste(big(f"{t}_fm_op_tone"), (i * (480 + gap), 0))
    fm6.save(out / "fm6_themes.png", optimize=True)
    new = Image.new("RGBA", (3 * 480 + 2 * 24, 480), (0, 0, 0, 0))   # (1.2) its release notes' picture
    for i, s in enumerate(["SKIFF_presets_bass", "CRYPT_drive_fold", "NEON_perform_held"]):
        new.paste(big(s), (i * (480 + gap), 0))
    new.save(out / "new_1_2.png", optimize=True)
    new = Image.new("RGBA", (3 * 480 + 2 * 24, 480), (0, 0, 0, 0))   # (1.3) its release notes' picture
    for i, s in enumerate(["SKIFF_insert_flanger", "NEON_lfo_2", "QUARK_learn_cc"]):
        new.paste(big(s), (i * (480 + gap), 0))
    new.save(out / "new_1_3.png", optimize=True)
    hero = Image.new("RGB", (3 * 480 + 4 * 16, 2 * 480 + 3 * 16), (20, 20, 20))   # (16 px margins and gaps)
    for i, s in enumerate(HERO):
        hero.paste(big(f"SKIFF_{s}"), (16 + (i % 3) * 496, 16 + (i // 3) * 496))
    hero.save(out.parent / "skiff.png", optimize=True)
    print(f"wrote {len(SHOTS)} screens, themes.png and ../skiff.png to {out}")


if __name__ == "__main__":
    main(sys.argv[1])
