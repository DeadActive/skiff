#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build Felucca: the app, the update loader and an installable .fwsc package.

  tools/build.py [--release X.Y[-suffix]] [--board fm1|smk37] [--model pro|elite|mke]

Outputs in build/: felucca.bin (app), loader/ota.bin (update loader),
felucca.fwsc (package). A release build (--release X.Y) writes skiff-X.Y.fwsc and a folder
release-X.Y/ with the package, the app, SHA256SUMS, the samples' attributions and the licence files.
See BUILDING.md for the toolchain and the SDK.

BOARD=smk37 (or --board smk37) builds Skiff for the M-VAVE SMK-37 Pro (after genkigenki's SMK-37 layer for Felucca):
-DBOARD_SMK37=1 for the app and the loader, identity SMK-37 Pro_700 (release SMK-37 Pro_7XY), the SMK flash layout
(fm1pkg_make.py), the package build/skiff-smk37.fwsc (skiff-smk37-X.Y.fwsc for a release). MODEL=elite / mke (or
--model) with smk37: the same build under the SMK-37 Elite's or the MKE-P37's model name (identity, loader, update
record; -DSMK_MODEL), so the stock updater still takes that unit's factory package: skiff-smk37-elite.fwsc, -mke.fwsc.
The default stays the FM-1, whose build gets no new flag.

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.

FELUCCA_BLE=1 (BLE MIDI: 2026 DEADACTIVE, from Felucca's Bluetooth build, DeadActive/Felucca ble-kit): the Bluetooth
build, for the FM-1 only (BOARD=smk37 refuses it). firmware/src/ble/ (its own unit) and the JieLi BT libraries
(tools/ble_libs.py, AC79_BT_SDK) are linked in through LTO with firmware/app_ble.ld; for the flash room its built-in
samples are a pack (tools/gen_samples.py FELUCCA_SMP_PACK: build/skiff-samples-VER.bin, the plain build's sample bytes,
installed over USB into USR2 and USR3's flash: one user slot, USR1), it leaves out the pixel faces of CRYPT and NEON
(gfx.c), USB audio's 48 kHz (FELUCCA_UAC_48K=0) and the top bar's Bluetooth icon, its BLE unit is built for size (-Oz)
without its console words and diagnostics (BLE_CONSOLE=0), its delay line is 1.11 s (fx.c), and it is built with one
core (FELUCCA_CPU2=0); the build stops on any of its gates (the loader pin, the
update path, no BT at boot, RAM code: docs/ble/). The package is felucca-ble.fwsc. BLE_MEASURE=1: the regions widened
so an image that does not fit can be measured; never packaged. Unset: the build is as before.
"""
import argparse
import hashlib
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
FW = SRC / "firmware"
OUT = SRC / "build"
GEN = OUT / "gen"
LDR = OUT / "loader"
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
# the staged loader (its ota.bin, the package's UFW entry) must fit what the stock app leaves for one: on the SMK-37
# its staging at 0xBD000, its update record at 0xBEF00 (SMK37.md), where a bigger one may spill into BTIF / USRTRIM.
# The FM-1's stock V15 gap is not measured yet: the same limit holds for it until it is
LDR_MAX = 0xBEF00 - 0xBD000
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
SDK_MOUNT = None                    # Bluetooth builds: the JieLi BT SDK (tools/ble_libs.py), mounted read-only at /sdk
BLE_SMP_PACK = GEN / "skiff_samples.pack"   # Bluetooth builds: the built-in samples as a pack, not in the image
                                    # (tools/gen_samples.py FELUCCA_SMP_PACK; docs/ble/FIT.md); build/skiff-samples-VER.bin
BLE_POOL_FLOOR = 8192               # Bluetooth builds: the pool spare kept. The user (2026-10-23) allowed less, as SLOOP's,
                                    # down to what the build keeps; with the delay line at 1.11 s it keeps 11,424 B, so the
                                    # plain build's 8 KiB holds (docs/ble/FIT.md); RAM keeps its 4 KiB
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

PRODUCT = "FM-1_700"                # package identity; release builds are FM-1_7XY (Felucca's are FM-1_9XY)
BOARD = "fm1"                       # fm1 (default) or smk37: BOARD env or --board
BOARD_PRODUCT = {"fm1": "FM-1_700", "smk37": "SMK-37 Pro_700"}   # a dev build's identity
BOARD_PREFIX = {"fm1": "FM-1_7", "smk37": "SMK-37 Pro_7"}        # a release build's: prefix + X + Y
BOARD_DEFS = {"fm1": [], "smk37": ["-DBOARD_SMK37=1"]}           # the FM-1 build gets no new flag (byte-identical)
BOARD_STEM = {"fm1": "skiff", "smk37": "skiff-smk37"}            # a release package: STEM-X.Y.fwsc
# smk37: the keyboard's model name (MODEL env or --model); one hardware, three names. pro adds no flag
MODELS = {"pro": "SMK-37 Pro", "elite": "SMK-37 Elite", "mke": "MKE-P37"}
VERSION = None                      # FELUCCA_VERSION for release builds (default: firmware/src/felucca.c)
# FELUCCA_CPU2 (firmware/src/felucca.c), unset or 1, the default (Skiff 1.3, as Felucca 1.6.1): the second core is built
# in, its entry and stacks (firmware/hal/fm1_cpu1.S) linked; MENU > SYSTEM > CPU 2 CORES starts it (1 CORE by default:
# as 1.2). FELUCCA_CPU2=0: none of it is built
CPU2 = os.environ.get("FELUCCA_CPU2") != "0"


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd SRC; paths relative to SRC"""
    rel = [str(Path(a).resolve().relative_to(SRC)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():
        sdk = ["-v", f"{SDK_MOUNT}:/sdk:ro"] if SDK_MOUNT else []
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work", *sdk,
               "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    for _ in range(5):              # Docker's amd64 emulation now and then refuses to start a tool: again
        r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
        if not (r.returncode and use_docker() and "exec format error" in r.stdout + r.stderr):
            break
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    with ThreadPoolExecutor(len(cmds)) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate():
    """generated headers (UI fonts, icons, keycaps, palettes, tables, samples)"""
    GEN.mkdir(parents=True, exist_ok=True)
    for old in ("felucca_font.h", "felucca_icons.h"):     # headers of the bitmap font and icon atlas
        (GEN / old).unlink(missing_ok=True)
    tools = SRC / "tools"
    cmds = [[tools / "gen_aa_font.py", GEN / "ui_fonts.h", "--preset", "barlow-condensed"],
            [tools / "gen_aa_icons.py", GEN / "ui_icons.h"],
            [tools / "gen_aa_keycaps.py", GEN / "ui_keycaps.h"],
            [tools / "gen_ui_palettes.py", GEN / "ui_palettes.h"],
            [tools / "gen_pixel_font.py", GEN / "ui_pixfont.h"],
            [tools / "gen_tables.py", GEN / "felucca_tables.h"],
            [tools / "gen_fm6_patches.py", GEN / "felucca_fm6.h"],
            [tools / "gen_samples.py", GEN / "felucca_samples.h"]]
    procs = [subprocess.Popen([sys.executable, *map(str, c)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True) for c in cmds]
    failed = []
    for c, p in zip(cmds, procs):
        sys.stdout.write(p.communicate()[0])
        if p.returncode:
            failed.append(c[0].name)
    if failed:
        raise SystemExit(f"build: {', '.join(failed)} failed")


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = FW / "loader"
    flags = [*CFLAGS, *BOARD_DEFS[BOARD], "-Ifirmware/hal", "-Ifirmware/src", f'-DFELUCCA_ID="ota-{PRODUCT}"']
    flags = ["-Oz" if f == "-Os" else f for f in flags]   # (LDR_MAX: the smallest code the compiler makes)
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    if len(ota) > LDR_MAX:
        raise SystemExit(f"loader: ota.bin {len(ota)} B, over the {LDR_MAX} B the stock app stages")
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B, {LDR_MAX - len(ota)} B under the stock staging")
    if is_ble():                    # gate 1: a Bluetooth build ships the base release's update loader, no other
        pin, got = ble_base("LOADER_SHA256"), hashlib.sha256(ota).hexdigest()
        if got != pin:
            raise SystemExit(f"loader: ota.bin {got} is not the pinned loader (tools/ble_base.txt)")
    return ota


def is_ble():
    return os.environ.get("FELUCCA_BLE") == "1"


def ble_base(key):
    """a value of tools/ble_base.txt (BLE_BASE_FILE: another copy, for the gates' own tests)"""
    f = Path(os.environ.get("BLE_BASE_FILE") or SRC / "tools" / "ble_base.txt")
    for ln in f.read_text().splitlines():
        if ln.startswith(key + " "):
            return ln.split()[1]
    raise SystemExit(f"build: {f} has no {key}")


def write_smk_ldr(ota):
    """SMK-37: the app carries its own loader (build/gen/smk_ldr.h, ota.c) and stages it for M-VAVE's packages"""
    rows = ",\n".join("    " + ", ".join(f"0x{b:02x}" for b in ota[i:i + 16]) for i in range(0, len(ota), 16))
    (GEN / "smk_ldr.h").write_text(
        "/* generated by tools/build.py: this build's update loader (build/loader/ota.bin), staged by ota.c in place of\n"
        " * M-VAVE's for an official package (SMK37.md) */\n"
        f"#define SMK_LDR_LEN {len(ota)}u\n"
        f"_Static_assert(SMK_LDR_LEN <= {LDR_MAX}u, \"the SMK loader must fit the stock app's staging\");\n"
        f"static const uint8_t SMK_LDR[SMK_LDR_LEN] = {{\n{rows}\n}};\n")


# ---- app

REGION = r"^\s*(\w+)\s*\([rwx]+\)\s*:\s*ORIGIN\s*=\s*(\w+),\s*LENGTH\s*=\s*(\w+)"


def ld_regions(script=None):
    """a linker script's MEMORY regions (default firmware/app.ld): name -> (origin, length)"""
    regs = {}
    for m in re.finditer(REGION, (script or FW / "app.ld").read_text(), re.M):
        n = m.group(3)
        regs[m.group(1)] = (int(m.group(2), 0), int(n[:-1], 0) * 1024 if n.endswith("K") else int(n, 0))
    return regs


def ble_regions_errors(ble, plain):
    """firmware/app_ble.ld against app.ld: the same map, but for two moves inside the same SRAM (docs/ble/FIT.md):
    RAMTEXT may run on to 0x01C07F00 (the rest of the RAM-run debug area: crt0.S keeps its STATUS words at
    0x01C07F20..), and the RAM / POOL boundary may move (RAM still ends where the POOL starts, the POOL where it ended)"""
    e = []
    for name in ("XIP", "NOINIT"):
        if ble.get(name) != plain.get(name):
            e.append(f"{name} differs")
    if set(ble) != set(plain):
        e.append("other regions")
    elif ble["RAMTEXT"][0] != plain["RAMTEXT"][0] or not plain["RAMTEXT"][1] <= ble["RAMTEXT"][1] <= 0x7F00:
        e.append("RAMTEXT is not app.ld's, grown up to 0x01C07F00 at most")
    elif ble["RAM"][0] != plain["RAM"][0] or sum(ble["RAM"]) != ble["POOL"][0] or sum(ble["POOL"]) != sum(plain["POOL"]):
        e.append("RAM / POOL are not app.ld's SRAM split another way")
    return e


def core_object(flags, out):
    """felucca.c -> out.o as the app's core is compiled (the main-loop functions built for size: tools/size_fns.py)"""
    if os.environ.get("FELUCCA_SIZE") == "0":
        tc("cc", *flags, "-c", FW / "src" / "felucca.c", "-o", out.with_suffix(".o"))
        return
    tc("cc", *flags, "-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns", "-c", FW / "src" / "felucca.c", "-o",
       out.with_suffix(".ll"))
    subprocess.run([sys.executable, SRC / "tools" / "size_fns.py", out.with_suffix(".ll"),
                    out.with_name(out.name + "_size.ll")], check=True, stdout=subprocess.DEVNULL)
    tc("cc", *[f for f in flags if not f.startswith(("-I", "-D", "-W"))], "-c", out.with_name(out.name + "_size.ll"),
       "-o", out.with_suffix(".o"))


def gate2c_listings(flags):
    """gate 2c: the core compiled twice with the same flags, once as the plain build's (no FELUCCA_BLE) and once as
    the Bluetooth build's, both without global merging (which small statics share a block depends on the whole
    program: with it every function would address its data at other offsets), each linked with app.ld (the
    Bluetooth one with empty BLE entry points) -> their listings. What differs between them in an update-path
    function is what FELUCCA_BLE changes in that function's code, whatever inlining brought it there."""
    g = OUT / "gate2c"
    g.mkdir(exist_ok=True)
    nomerge = ["-mllvm", "-enable-global-merge=false"]
    out = {}
    for tag, fl, extra in (("plain", [f for f in flags if f != "-DFELUCCA_BLE=1"], []),
                           ("ble", flags, [OUT / "ble_stub.o"])):
        core_object([*fl, *nomerge], g / tag)
        tc("pi32v2/bin/ld", "-T", FW / "app.ld", OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o",
           *([OUT / "fm1_cpu1.o"] if CPU2 else []), g / f"{tag}.o", *extra, "-o", g / f"{tag}.elf")
        out[tag] = tc("common/bin/objdump", "-d", g / f"{tag}.elf")
        (g / f"{tag}.dis").write_text(out[tag])
    return out["plain"], out["ble"]


def build_app():
    flags = [*CFLAGS, *BOARD_DEFS[BOARD], "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen"]
    ble = is_ble()
    if ble:
        flags.append("-DFELUCCA_BLE=1")
        # the console's `ble` / `fml` words and the diagnostics they print: none in Skiff's Bluetooth build, for the room
        # of its sample pack (ble/ble_core.h BLE_CONSOLE; BLE_CONSOLE=1 brings them back, docs/ble/FIT.md)
        flags.append(f"-DBLE_CONSOLE={'1' if os.environ.get('BLE_CONSOLE') == '1' else '0'}")
    for flag in ("FELUCCA_FLASH", "FELUCCA_OTA", "FELUCCA_OTA_DRYRUN", "FELUCCA_OTA_RAMONLY", "FELUCCA_CDC",
                 "FELUCCA_UART", "FELUCCA_UAC", "FELUCCA_UAC_TONE", "FELUCCA_UAC_48K", "FELUCCA_ICONS", "FELUCCA_SLICE", "FELUCCA_FM4",
                 "FELUCCA_CDC_DEFAULT", "FELUCCA_CPU2"):
        v = os.environ.get(flag)    # unset: the default in firmware/src/felucca.c
        if v in ("0", "1"):
            flags.append(f"-D{flag}={v}")
    v = os.environ.get("FELUCCA_USB_LAYOUT")   # firmware/src/usb.c: how the USB composite presents itself (#67), 0..3
    if v in ("0", "1", "2", "3"):
        flags.append(f"-DFELUCCA_USB_LAYOUT={v}")
    flags.append(f'-DFELUCCA_ID="{PRODUCT}"')
    if VERSION:
        flags.append(f'-DFELUCCA_VERSION="{VERSION}"')
    # felucca.c goes to LLVM IR without the optimizer, the main-loop functions (UI, stores, editor) are
    # marked minsize (tools/size_fns.py), then the IR is compiled at -Os. FELUCCA_SIZE=0: -Os everywhere
    size = os.environ.get("FELUCCA_SIZE") != "0"
    cmain = (("cc", *flags, "-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns", "-c",
              FW / "src" / "felucca.c", "-o", OUT / "felucca.ll") if size else
             ("cc", *flags, "-c", FW / "src" / "felucca.c", "-o", OUT / "felucca.o"))
    asm, cfg_objs = [], []
    if ble:                         # (from Felucca's Bluetooth build, after Hortator's build.py, fm1-lsdj 548ce73)
        global SDK_MOUNT
        import ble_libs
        if not use_docker():
            raise SystemExit("build: FELUCCA_BLE builds run the toolchain in Docker (the SDK is mounted at /sdk)")
        ble_libs.libs()             # first: a missing SDK or a library that is not the pinned one stops here, named
        SDK_MOUNT = str(ble_libs.SDK)
        asm.append(("cc", "-c", FW / "hal" / "fm1_ctx.S", "-o", OUT / "fm1_ctx.o"))
        (OUT / "blecfg").mkdir(exist_ok=True)
        for src in ble_libs.SDK_CONFIG_SOURCES:     # the SDK's BT config as bitcode: LTO folds it into the libraries
            obj = OUT / "blecfg" / (Path(src).stem + ".o")
            asm.append(("cc", *ble_libs.SDK_CFLAGS, "-c", f"/sdk/{src}", "-o", obj))
            cfg_objs.append(obj)
        asm.append(("cc", "-mcpu=r3", "-Os", "-flto", "-Wall", "-c", SRC / ble_libs.LTO_STUBS, "-o",
                    OUT / "blecfg" / "ble_lto_stubs.o"))
        cfg_objs.append(OUT / "blecfg" / "ble_lto_stubs.o")
    (OUT / "fm1_cpu1.o").unlink(missing_ok=True)
    tc_all(("cc", "-c", FW / "crt0.S", "-o", OUT / "crt0.o"),
           ("cc", "-c", FW / "hal" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
           ("cc", "-c", FW / "hal" / "fm1_isr.S", "-o", OUT / "fm1_isr.o"),
           *([("cc", "-c", FW / "hal" / "fm1_cpu1.S", "-o", OUT / "fm1_cpu1.o")] if CPU2 else []),
           *asm, cmain)
    if size:
        subprocess.run([sys.executable, SRC / "tools" / "size_fns.py", OUT / "felucca.ll", OUT / "felucca_size.ll"],
                       check=True)
        tc("cc", *[f for f in flags if not f.startswith(("-I", "-D", "-W"))], "-c",
           OUT / "felucca_size.ll", "-o", OUT / "felucca.o")
    elf = OUT / "felucca.elf"
    ld = FW / "app.ld"
    if APP_SLOT != 0x8DFBC:          # another board's app slot (SMK-37 Pro): the same script, its XIP length
        txt = ld.read_text()
        if txt.count("LENGTH = 0x8DFBC") != 1:
            raise SystemExit("app.ld: the XIP LENGTH line was not found")
        ld = OUT / "app.ld"
        ld.write_text(txt.replace("LENGTH = 0x8DFBC", f"LENGTH = {APP_SLOT:#x}"))
    cpu1 = [OUT / "fm1_cpu1.o"] if CPU2 else []
    extra, tool = [], ("pi32v2/bin/ld",)
    if ble:                         # (only then: the plain link is as before)
        libdir = OUT / "blelibs"
        libdir.mkdir(exist_ok=True)
        libs = ble_libs.libs()
        for p in libs:              # inside SRC, so the Docker mount sees them
            shutil.copy(p, libdir / Path(p).name)
        objs = ble_libs.members(libdir)     # single SDK members (lbuf, circular_buf, wlc, encryption)
        # the BLE module, its own unit: no static data shared with the core's unit. Long calls, as the libraries
        # (large-program): the BLE unit calls RAM code (__wrap_memcpy, the delays). No global merging, as the
        # object Hortator's device tests ran
        # (Skiff: built for size, -Oz, for the room of its sample pack; docs/ble/FIT.md)
        tc("cc", *[("-Oz" if f == "-Os" else f) for f in flags if f != "-Ibuild/gen"], "-mllvm",
           "-enable-global-merge=false", "-mllvm", "-pi32v2-large-program=true", "-c", FW / "src" / "ble" / "ble.c",
           "-o", OUT / "ble.o")
        objs.append(OUT / "ble.o")
        extra = [OUT / "fm1_ctx.o", *cfg_objs, *objs, "--start-group", *[libdir / Path(p).name for p in libs],
                 "--end-group"]
        # the BT libraries are LLVM bitcode: link them as the SDK does (apps/demo/demo_ble/board/wl82/Makefile
        # LFLAGS), through the gold plugin with its codegen options; our objects stay native
        tool = (*tool, "--plugin", "/opt/jieli/pi32v2/bin/LLVMgold.so", "--plugin-opt=mcpu=r3",
                "--plugin-opt=-pi32v2-large-program=true", "--plugin-opt=-pi32v2-always-use-itblock=false",
                "--plugin-opt=-dont-used-symbol-list=" + ",".join(ble_libs.LOG_FUNCS),
                "--orphan-handling=error", "-Map", "build/felucca.map",   # every library section placed on purpose
                # the libraries' (and the BLE unit's) memcpy calls go to ble_port.c's RAM copy, __wrap_memcpy: the
                # radio calibration calls memcpy while the flash must not be read. --wrap redirects undefined
                # references only, so the core's unit, which defines memcpy, keeps calling its own
                "--wrap=memcpy")
        ld = FW / "app_ble.ld"
        e = ble_regions_errors(ld_regions(ld), ld_regions())
        if e:
            raise SystemExit(f"build: firmware/app_ble.ld's MEMORY block is not app.ld's: {'; '.join(e)}")
        if os.environ.get("BLE_MEASURE") == "1":
            # measuring only (never packaged, main()): the XIP to the flash's end, the pool to the noinit area (over
            # the stack symbols) and the RAM code to 0x01C07F00, so an image that does not fit yet still links
            pool_org = ld_regions(ld)["POOL"][0]
            lds = ld.read_text()
            lds, n = re.subn(r"(XIP\s+\(rx\)\s*: ORIGIN = 0x02000120, LENGTH = )\w+", r"\g<1>0xFFEE0", lds)
            lds, k = re.subn(r"(POOL\s+\(rw\)\s*: ORIGIN = \w+, LENGTH = )\w+",
                             rf"\g<1>{0x01C7C000 - pool_org:#x}", lds)
            lds, j = re.subn(r"(RAMTEXT\s*\(rwx\)\s*: ORIGIN = 0x01C00000, LENGTH = )\w+", r"\g<1>0x7F00", lds)
            # (Skiff: the pool may outgrow even that: 64 KB more, the noinit area moved past the SRAM's end. Such an
            # image never runs: it is only measured)
            lds = lds.replace(f"LENGTH = {0x01C7C000 - pool_org:#x}", f"LENGTH = {0x01C8C000 - pool_org:#x}", 1)
            lds, i = re.subn(r"(NOINIT\s+\(rw\)\s*: ORIGIN = )0x01C7C000", r"\g<1>0x01C8C000", lds)
            if n != 1 or k != 1 or j != 1 or i != 1:
                raise SystemExit("build: BLE_MEASURE: app_ble.ld's XIP / POOL / RAMTEXT lines not found")
            ld = OUT / "app_ble_measure.ld"
            ld.write_text(lds)
    tc(*tool, "-T", ld, OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o", *cpu1, OUT / "felucca.o", *extra,
       "-o", elf)
    if ble:                         # library code meant for RAM would have landed in XIP via *(.*_code): refuse
        # .volatile_ram_code (wl_rf_common's wf_rf_trim) is placed in .ram_text with the trim (app_ble.ld);
        # .bt_updata_ram_code = bredr_frame's BT-over-the-air-update helpers, never run, allowed in XIP (fm1-lsdj
        # docs/ble/LINK_NOTES.md: checked by hand)
        ok = {".volatile_ram_code", ".bt_updata_ram_code"}
        ram_code = sorted(set(re.findall(r"^\s*(\.\S*ram_code\S*)", (OUT / "felucca.map").read_text(), re.M)) - ok)
        if ram_code:
            raise SystemExit(f"build: BT library RAM code sections linked (not placed in RAM): {ram_code}")
    ref_dis = None
    if ble:                         # gate 2b's reference: the same core object, empty BLE entry points, app.ld
        tc("cc", *[f for f in flags if f != "-Ibuild/gen"], "-c", FW / "src" / "ble" / "ble_stub.c", "-o",
           OUT / "ble_stub.o")
        tc("pi32v2/bin/ld", "-T", FW / "app.ld", OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o", *cpu1,
           OUT / "felucca.o", OUT / "ble_stub.o", "-o", OUT / "felucca_ref.elf")
        ref_dis = tc("common/bin/objdump", "-d", OUT / "felucca_ref.elf")
        (OUT / "felucca_ref.dis").write_text(ref_dis)
        ref_dis = (ref_dis, *gate2c_listings(flags))
    for sect in ("text.bin", "data.bin", "ramtext.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_text", elf))
    (OUT / "felucca.dis").write_text(dis)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # .ram_text and .data follow .text at their load addresses; crt0 copies them by words
    for sect, lname in (("ramtext.bin", "_rt_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    (OUT / "felucca.bin").write_bytes(img)
    return bytes(img), syms, dis, rt, ref_dis, ld


def rt_reach(rt, hot):
    """Bluetooth builds (the radio calibration runs from .ram_text and calls within it; fm1-lsdj 548ce73): the RAM
    code below _rt_hot (the flash driver, the BLE unit's delays and the radio calibration) may call and jump only
    inside it, never through a register, and use no XIP address as data. (.ram_hot, after it, is audio code that never
    runs with the flash busy and may call XIP)"""
    def in_rt(a): return 0x01C00000 <= a < hot
    bad, calls = [], 0
    for ln in rt.splitlines():
        if not LINE.match(ln):
            continue
        if re.search(r"\b(call|goto)\s+r\d+\b", ln):
            bad.append(ln)
            continue
        for a in re.findall(r"<[^>]*:\s*([0-9a-f]+)\s*>", ln):
            if 0x02000000 <= int(a, 16) < 0x02100000 or (re.search(r"\b(call|goto)\b", ln) and not in_rt(int(a, 16))):
                bad.append(ln)
        calls += bool(re.search(r"\bcall\b", ln))
    return bad, calls


def check(img, syms, dis, rt, ble=False, measure=False, script=None):
    errors, notes = [], []
    soft = notes if measure else errors          # BLE_MEASURE: the budgets are reported, not enforced
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    m = re.search(r"^([0-9a-f]+) .*\s_rt_hot$", syms, re.M)   # (Skiff) from _rt_hot: .ram_hot, audio code that
    hot = int(m.group(1), 16) if m else 1 << 32                  # never runs with the flash busy (app.ld): it may call
    rt_ins = [(int(mm.group(1), 16), ln) for ln in rt.splitlines() if (mm := LINE.match(ln))]
    rt_calls = [ln for a, ln in rt_ins if a < hot and re.search(r"\bcall\b", ln)]
    if ble:                         # gate 4: the radio calibration's RAM code calls inside .ram_text only
        rt_bad, n_calls = rt_reach("\n".join(ln for a, ln in rt_ins if a < hot), hot)
        if rt_bad:
            errors.append(f".ram_text reaches outside RAM: {rt_bad[:3]}")
        else:
            notes.append(f".ram_text: {sum(a < hot for a, _ in rt_ins)} insns, {n_calls} calls, all inside .ram_text "
                         f"below .ram_hot, no XIP address; .ram_hot: {sum(a >= hot for a, _ in rt_ins)} insns")
    elif rt_calls:                  # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        notes.append(f".ram_text: {sum(a < hot for a, _ in rt_ins)} insns, no calls; "
                     f".ram_hot: {sum(a >= hot for a, _ in rt_ins)} insns")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    if len(img) > APP_SLOT:
        soft.append(f"image {len(img)} B exceeds the app slot ({APP_SLOT} B) by {len(img) - APP_SLOT} B")

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    if not ble:                     # (the plain build's checks as they were)
        bss = sym("_bss_end") - 0x01C08000
        pool = sym("_pool_end") - sym("_pool_start")
        rtx = sym("_ramx_end") - sym("_rt_start")      # (Skiff) the RAM code and .ramx after it (app.ld)
        notes.append(f"image {len(img)} B; RAM .data+.bss {bss} B of 98304; pool {pool} B of {0x57000}; "
                     f"RAM code + .ramx {rtx} B of {0x6000}")
        if bss > 96 * 1024:
            errors.append("RAM region overflow")
        if 0x57000 - pool < 8192:                     # keep >= 8 KiB of the pool spare (app.ld POOL)
            errors.append(f"pool headroom {0x57000 - pool} B < 8192 B")
        return errors, notes
    region = ld_regions(FW / "app_ble.ld")        # (the Bluetooth build: its own split of the same SRAM, app_ble.ld)
    (ram_org, ram_len), pool_len, rt_len = region["RAM"], region["POOL"][1], region["RAMTEXT"][1]
    bss = sym("_bss_end") - ram_org
    pool = sym("_pool_end") - sym("_pool_start")
    rtx = sym("_ramx_end") - sym("_rt_start")
    notes.append(f"image {len(img)} B ({APP_SLOT - len(img)} B of the slot free); RAM .data+.bss {bss} B of "
                 f"{ram_len}; pool {pool} B of {pool_len}; RAM code + .ramx {rtx} B of {rt_len}")
    if ram_len - bss < 4096:                      # keep >= 4 KiB of .bss spare (state the audio side touches)
        soft.append(f"RAM headroom {ram_len - bss} B < 4096 B")
    if pool_len - pool < BLE_POOL_FLOOR:          # keep the Bluetooth build's pool floor spare
        soft.append(f"pool headroom {pool_len - pool} B < {BLE_POOL_FLOOR} B (BLE_POOL_FLOOR)")
    if rtx > rt_len:
        soft.append(f"RAM code + .ramx {rtx} B over RAMTEXT's {rt_len} B")
    return errors, notes


# Every hardware register access lives in hal/. In src/ and loader/ (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted((FW / "hal").glob("*.h")):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    ble = [f for f in (FW / "src" / "ble").glob("*.[ch]") if f.name != "ble_rf_tables.c"]   # (the SDK's radio tables)
    for f in sorted([*(FW / "src").glob("*.[ch]"), *ble, *(FW / "loader").glob("*.c")]):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(FW)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside hal/ ({m.group(0).strip()}); add a hal/ helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: HAL register macro {sorted(used)[0]} outside hal/; use a hal/ helper")
    return errors


def main():
    global PRODUCT, VERSION, BOARD, APP_SLOT
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", metavar="X.Y", help="release build: identity FM-1_7XY, version string vX.Y")
    ap.add_argument("--board", choices=sorted(BOARD_PRODUCT), default=None,
                    help="target board: fm1 (default) or smk37 (default: $BOARD, else fm1)")
    ap.add_argument("--model", choices=sorted(MODELS), default=None,
                    help="smk37: the model name, pro (default), elite or mke (default: $MODEL, else pro)")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    a = ap.parse_args()
    BOARD = (a.board or os.environ.get("BOARD") or "fm1").lower().replace("-", "")
    if BOARD not in BOARD_PRODUCT:
        raise SystemExit(f"BOARD={BOARD}: use fm1 or smk37")
    model = (a.model or (os.environ.get("MODEL") if BOARD == "smk37" else None) or "pro").lower()
    if model not in MODELS or (model != "pro" and BOARD != "smk37"):
        raise SystemExit(f"MODEL={model}: pro, elite or mke, with BOARD=smk37")
    ble = is_ble()
    measure = ble and os.environ.get("BLE_MEASURE") == "1"
    if ble and BOARD != "fm1":      # (the Bluetooth build is the FM-1's: the SMK-37's radio, flash and RAM are not)
        raise SystemExit(f"build: FELUCCA_BLE=1 is for BOARD=fm1 only, not {BOARD}")
    fm1pkg_make.set_board(BOARD)
    APP_SLOT = fm1pkg_make.APP_SLOT
    PRODUCT = BOARD_PRODUCT[BOARD]
    stem = BOARD_STEM[BOARD]
    name = "felucca.fwsc" if BOARD == "fm1" else f"{stem}.fwsc"
    if model != "pro":              # another model name: identity, loader and update record (fm1_cc.h SMK_MODEL)
        BOARD_DEFS[BOARD] = [*BOARD_DEFS[BOARD], f'-DSMK_MODEL="{MODELS[model]}"']
        PRODUCT = PRODUCT.replace(MODELS["pro"], MODELS[model])
        BOARD_PREFIX[BOARD] = BOARD_PREFIX[BOARD].replace(MODELS["pro"], MODELS[model])
        stem += "-" + model
        name = f"{stem}.fwsc"
    if a.release:                   # one digit each: the identity has room for two (X.Y.Z and X.Y.Z.W keep X.Y's)
        m = re.fullmatch(r"(\d)\.(\d)(?:\.\d){0,2}(-[A-Za-z0-9]+)?", a.release)
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y, X.Y.Z, X.Y.Z.W or X.Y-suffix, one digit each")
        PRODUCT = BOARD_PREFIX[BOARD] + m[1] + m[2]
        VERSION = "v" + a.release.lower()      # e.g. v1.0, v1.1-rc1
        name = f"{stem}-{a.release}.fwsc"
    if ble:                         # a gated Bluetooth build is packaged under its own name
        global CPU2
        name = name.replace("felucca", "felucca-ble", 1).replace(stem, stem + "-ble", 1)
        os.environ.setdefault("FELUCCA_SMP_PACK", str(BLE_SMP_PACK))   # (tools/gen_samples.py: the flash room)
        # one core (FELUCCA_CPU2=0): the second core's stacks and mailbox (6 KB of .data + .bss) and the BT stack's do
        # not both fit the RAM with its 4 KB kept spare, and the two are untried together on the FM-1. CPU 2 CORES is
        # an opt-in beta, 1 CORE by default; the plain build keeps it
        os.environ.setdefault("FELUCCA_CPU2", "0")
        CPU2 = os.environ["FELUCCA_CPU2"] != "0"
        os.environ.setdefault("FELUCCA_UAC_48K", "0")   # USB audio at 44.1 kHz only: its 48 kHz resampler (8 KB) for the
                                                        # flash room (the user, 2026-10-23; docs/ble/FIT.md)
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            print(f"warning: SDK {rel} differs from AC79NN_SDK_V1.2.1; the package will not match the reference")
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(2) as ex:
        gen, ldr = ex.submit(generate), ex.submit(build_loader)
        gen.result()
        ota = ldr.result()
    if BOARD == "smk37":
        write_smk_ldr(ota)
    img, syms, dis, rt, ref_dis, script = build_app()
    errors, notes = check(img, syms, dis, rt, ble, measure, script)
    if ble:                         # gates 2-3: every Bluetooth build stops on them
        import check_update_path as cup
        import check_ble_boot as cbb
        base = cup.last_release()
        ref_dis, plain_nm, ble_nm = ref_dis
        ref = cup.selected(cup.functions(ref_dis))
        e2a = cup.source_errors(base)
        e2b = cup.compare(dis, cup.render(ref))
        e2c, same2c, known2c = cup.compare_builds(plain_nm, ble_nm)
        e3 = cbb.reachable_starts(dis)
        errors += [f"gate 2a: {e}" for e in e2a] + [f"gate 2b: {e}" for e in e2b] + [f"gate 2c: {e}" for e in e2c]
        errors += [f"gate 3: BT start-up reachable at boot: {', '.join(e3)}"] if e3 else []
        notes.append("gate 1: the update loader = the pinned one (tools/ble_base.txt)")
        if not e2a:
            notes.append(f"gate 2a: the update path's sources = {base}'s")
        if not e2b:
            notes.append(f"gate 2b: the update path's machine code = the reference core's ({len(ref)} functions)")
        if not e2c:
            notes.append(f"gate 2c: the update path compiled for the Bluetooth build = the plain build's "
                         f"({same2c} functions identical; differing as listed in check_update_path.py: "
                         f"{', '.join(known2c) or 'none'})")
        if not e3:
            notes.append("gate 3: no BT start-up reachable at boot (only through ble_user_start)")
    hal_err = mmio_check()
    errors += hal_err
    if not hal_err:
        notes.append("register access: hal/ only (src/, loader/ clean)")
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    if measure:
        print("measured only (BLE_MEASURE=1): no package")
        return 0
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
    att = SRC / "assets" / "samples-cc0" / "ATTRIBUTION.txt"
    if att.exists():
        shutil.copy(att, OUT / "ATTRIBUTION.txt")
    print(f"app      {OUT / 'felucca.bin'}  {len(img)} B")
    if ble and os.environ.get("FELUCCA_SMP_PACK"):  # the built-in pack beside the package (the site's install step)
        ver = (VERSION or re.search(r'define FELUCCA_VERSION "([^"]+)"', (FW / "src" / "felucca.c").read_text())[1])
        pack = OUT / f"skiff-samples-{ver.lstrip('v')}.bin"
        shutil.copy(os.environ["FELUCCA_SMP_PACK"], pack)
        print(f"pack     {pack}  {pack.stat().st_size} B (the built-in samples: the site's INSTALL SAMPLES step)")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}" + (f", board {BOARD}" if BOARD != "fm1" else ""))
    if a.release:                   # what a release carries: the package, the app and every licence they need
        rel = OUT / f"release-{a.release}"
        shutil.rmtree(rel, ignore_errors=True)
        (rel / "LICENSES").mkdir(parents=True)
        app = f"{stem}-{a.release}-app.bin"
        (rel / name).write_bytes(pkg)
        (rel / app).write_bytes(img)
        (rel / "SHA256SUMS").write_text("".join(f"{hashlib.sha256(b).hexdigest()}  {n}\n"
                                                for n, b in ((name, pkg), (app, bytes(img)))))
        for f in sorted((SRC / "LICENSES").glob("*.txt")):
            shutil.copy(f, rel / "LICENSES" / f.name)
        for doc in ("LICENSE", "LICENSING.md"):
            shutil.copy(SRC / doc, rel / doc)
        if att.exists():
            shutil.copy(att, rel / "ATTRIBUTION.txt")
        shutil.copy(SRC / "assets" / "breaks" / "ATTRIBUTION.txt", rel / "ATTRIBUTION-BREAKS.txt")   # (Skiff phase 9)
        print(f"release  {rel}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
