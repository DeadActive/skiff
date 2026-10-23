#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# BLE MIDI: 2026 DEADACTIVE
"""Install Skiff's built-in sample pack on an FM-1 running the Bluetooth build, over USB-MIDI.

  fm1_sample_pack.py info                       what the FM-1 has, and which pack its firmware wants
  fm1_sample_pack.py install build/skiff-samples-1.4.bin
  fm1_sample_pack.py check PACK                 the file alone: its header, length, CRC (no device)

The Bluetooth build keeps its built-in samples (PIANO, FLUTE, SAX, SLICE's BREAK and FUNK) in this pack, in the
flash of user slots USR2 and USR3 (one user slot, USR1, is left: anything in USR2 / USR3 is replaced). The pack must
be the one built with the firmware (build/skiff-samples-VER.bin: its length and id are checked by the FM-1). Protocol:
web/EDITOR_PROTOCOL.md, PACK (90): INFO, BEGIN, WRITE, END (the header written last). Stop the music first.
Needs mido (and a backend such as python-rtmidi) for the device commands.
"""
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fm1_sample_upload import Link, pack7  # noqa: E402

PACK, HDR, MAGIC = 90, 256, 0x4B504B53
RC = {0: "done", 1: "a bad request", 2: "not this firmware's pack", 3: "stop the music first", 4: "a flash error",
      5: "the data's CRC wrong (install again)"}


def read_pack(path):
    """the file -> (header fields, the whole file); exits on a broken one"""
    b = Path(path).read_bytes()
    if len(b) < HDR:
        sys.exit(f"{path}: too short for a pack")
    magic, ver, n, crc, pid = struct.unpack("<5I", b[:20])
    if magic != MAGIC or ver != 1 or len(b) != HDR + n or zlib.crc32(b[HDR:]) & 0xFFFFFFFF != crc:
        sys.exit(f"{path}: not a Skiff sample pack (magic, version, length or CRC)")
    return (n, crc, pid), b


def info(link):
    r = link.req(PACK, [0])
    if len(r) < 11 or r[0] != 0:
        sys.exit("no PACK reply: this firmware is not Skiff's Bluetooth build")
    n = r[2] | r[3] << 7 | r[4] << 14
    pid = sum(r[5 + i] << (7 * i) for i in range(5)) & 0xFFFFFFFF
    return r[1], n, pid


def install(link, path):
    (n, _, pid), b = read_pack(path)
    state, want_n, want_id = info(link)
    if (n, pid) != (want_n, want_id):
        sys.exit(f"{path} is not the pack this firmware was built with (it wants {want_n} B, id {want_id:08X})")
    if state == 1:
        print("the FM-1 has this pack already: installing again")
    rc = link.req(PACK, [1])[-1]
    if rc:
        sys.exit(f"BEGIN: {RC.get(rc, rc)}")
    for off in range(HDR, len(b), 256):
        chunk = b[off:off + 256]
        r = link.req(PACK, [2, off & 127, off >> 7 & 127, off >> 14 & 127] + pack7(chunk))
        if r[-1]:
            sys.exit(f"WRITE at {off}: {RC.get(r[-1], r[-1])}")
        print(f"\r{(off - HDR + len(chunk)) * 100 // n:3d} %", end="", flush=True)
    print()
    rc = link.req(PACK, [3] + pack7(b[:20]), 5.0)[-1]
    if rc:
        sys.exit(f"END: {RC.get(rc, rc)}")
    print("the built-in samples are installed")


def main():
    a = sys.argv[1:]
    if a[:1] == ["check"] and len(a) == 2:
        (n, crc, pid), _ = read_pack(a[1])
        print(f"{a[1]}: a Skiff sample pack, {n} B, CRC {crc:08X}, id {pid:08X}")
    elif a[:1] == ["info"] and len(a) == 1:
        state, n, pid = info(Link())
        what = ("none", "installed (this firmware's)", "another pack (install this firmware's)")[state]
        print(f"pack: {what}; the firmware wants {n} B, id {pid:08X}")
    elif a[:1] == ["install"] and len(a) == 2:
        install(Link(), a[1])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
