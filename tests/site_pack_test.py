#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# BLE MIDI: 2026 DEADACTIVE
"""web/make_site.py --pack (the Bluetooth build's site): the sample pack is published beside the package and the
installer has its INSTALL BUILT-IN SAMPLES step (web/pack_step.js) pointing at it; without --pack the installer is the
same as before (no step, no pack).
  site_pack_test.py PACKAGE PACK OUTDIR"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]


def site(pkg, out, *opts):
    shutil.rmtree(out, ignore_errors=True)
    r = subprocess.run([sys.executable, str(SRC / "web" / "make_site.py"), *opts, "--no-try", str(pkg), "1.4-ble",
                        str(out)], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    return (out / "webapp" / "installer" / "index.html").read_text(encoding="utf-8")


def main(pkg, pack, out):
    out = Path(out)
    with_pack = site(pkg, out / "ble", "--pack", pack)
    plain = site(pkg, out / "plain")
    name = Path(pack).name
    assert (out / "ble" / "firmware" / name).read_bytes() == Path(pack).read_bytes(), "the pack is not published"
    assert f'"samples": "../../firmware/{name}"' in with_pack, "the installer does not point at the pack"
    assert with_pack.count('id="pack-go"') == 1 and "[90, [3," in with_pack, "no INSTALL BUILT-IN SAMPLES step"
    assert "USR2 and USR3" in with_pack, "the step does not warn about USR2 / USR3"
    assert "pack-go" not in plain and "samples" not in re.search(r"const meta = (\{.*?\});", plain).group(1), \
        "the plain site has the step"
    assert not list((out / "plain" / "firmware").glob("skiff-samples-*.bin")), "the plain site has a pack"
    print("site_pack_test: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:4]))
