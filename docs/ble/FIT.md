# Skiff's Bluetooth build: does it fit?

BLE MIDI: 2026 DEADACTIVE.

Skiff 1.4 with Felucca's Bluetooth (DeadActive/Felucca `ble-kit`), `FELUCCA_BLE=1 ./build.sh`, the FM-1 only
(`BOARD=smk37` refuses it). Measured 2026-10-23 against the app slot (581,564 B), the RAM (.data + .bss, 4 KB kept
spare) and the pool (8 KB kept spare). `FELUCCA_BLE=1 BLE_MEASURE=1 ./build.sh` measures a build that does not fit
(the regions widened; never packaged).

## What the user decided to leave out (the Bluetooth build only)

| | flash B |
| --- | ---: |
| PIANO, FLUTE, SAX (`BLE_SMP_DROP`, a sine and NO SAMPLE in their place) | 86,338 |
| SLICE's BREAK (FUNK and the user slots stay) | 22,050 + its slice table |
| CRYPT's and NEON's pixel faces (`AF_*_PX`: those themes draw with the regular faces) | ~11,000 |
| the second core (`FELUCCA_CPU2=0`) | 2,832 flash, 6,084 RAM |

## Measured

| | image B (slot free) | `.data + .bss` B (of 98,304) | pool B (of 356,352) | RAM code + `.ramx` B |
| --- | ---: | ---: | ---: | ---: |
| Skiff 1.4, plain | 581,352 (212) | 86,020 | 346,316 | 24,080 of 24,576 |
| plain, one core | 578,520 (3,044) | 79,936 | 346,316 | |
| **the whole Bluetooth build, the cuts above** | **604,684 (-23,120)** | **94,096 (4,208 spare)** | **377,696 (-21,344; -29,536 with the 8 KB kept)** | 27,936 of 32,512 |

The Bluetooth side costs about 148 KB of flash here (the stack, the core's side, the device role, the page), 14.2 KB of
`.data + .bss` and 31.0 KB of pool: the BT heap (23,552 B) and the four task slots (7,440 B). The earlier
estimates (141 KB, 23.7 KB of pool) predate the larger heap and Skiff's page.

The RAM code: the radio calibration's 4 KB of RAM code do not fit beside Skiff's `.ram_hot` and `.ramx` in
0x01C00000..0x01C06000, so `app_ble.ld` lets RAMTEXT run on to 0x01C07F00 (the rest of the RAM-run debug area;
crt0.S's STATUS words at 0x01C07F20 stay out of it).

## What it costs on Skiff

About **148 KB of flash** (the stack, the core's side, the device role, the page), **14.2 KB of `.data + .bss`** and
**31 KB of pool** (the BT heap 23,552 B and the four task slots 7,440 B). The estimates from Felucca (141 KB, 23.7 KB of
pool) predate the larger heap and Skiff's page.

## The build (the user's decisions, 2026-10-23)

Besides the cuts above, the Bluetooth build leaves out SLICE's FUNK (as BREAK: no material, a sine and NO SAMPLE for a
sound that picks it; no sound the FM-1 starts with uses it, tests/ble_fit_test.c) and USB audio's 48 kHz
(`FELUCCA_UAC_48K=0`: 44.1 kHz only), and its delay line is 49,152 samples, 1.11 s (`fx.c` `DLY_LEN`, 1.49 s in the
plain build; every TIME clamps inside it, tests/ble_fit_test.c). The pool floor is `BLE_POOL_FLOOR` in `build.py`:
the user allowed a smaller one (as SLOOP's), but the build keeps more than the plain build's 8 KB, so it stays 8,192.
The SMC-Mixer on six strips (1..6 the tracks, faders 7 and 8 nothing): approved.

| | image B (slot free) | `.data + .bss` B (of 98,304) | pool B (of 356,352) | RAM code + `.ramx` B |
| --- | ---: | ---: | ---: | ---: |
| **the Bluetooth build** | **578,164 (3,400)** | **93,808 (4,496 spare)** | **344,928 (11,424 spare)** | 27,936 of 32,512 |

Every gate passes (1, 2a-2c, 3, the RAM code's reach, the registers). Before these decisions it was 23,120 B over the
slot and 29,536 B short of pool (below).

## Before the decisions


- **Flash: 23,120 B over the slot**, with no margin. What is left to take, each the user's call: SLICE's FUNK
  (~16.6 KB with its slice table), USB audio's 48 kHz (`FELUCCA_UAC_48K=0`, its resampler's table 7.7 KB plus code),
  the device role (~6.3 KB), the SMC-Mixer layout (~1.7 KB).
- **Pool: 29,536 B short** (8 KB kept spare); the RAM has only ~100 B to give (it keeps 4 KB). The delay line
  (`fx.c` `dly_buf`, 131,072 B, 1.49 s) is a power of two: half of it (0.74 s, a quarter note down to 81 BPM) frees
  65,536 B, enough with room to spare.

Measured what-ifs (scratch builds, not in the tree):

| also left out | image B (slot free) | pool B (of 356,352) |
| --- | ---: | ---: |
| USB audio's 48 kHz (`FELUCCA_UAC_48K=0`) | 596,412 (-14,848) | 377,696 |
| + SLICE's FUNK | 578,180 (3,384) | 377,696 |
| + the delay line halved (`fx.c` `DLY_LEN` 32768, 0.74 s) | 578,180 (3,384) | 312,160 (44,192 spare) |

With all three every gate passes (gates 1, 2a-2c, 3, 4 in a BLE_MEASURE build).

## The Bluetooth build (2026-10-24): the samples as a pack, every BLE MIDI feature

The user's decisions: the built-in samples (PIANO, FLUTE, SAX, SLICE's BREAK and FUNK) come back as a pack in USR2 and
USR3's flash (one user slot, USR1), every Bluetooth feature stays (host and device roles, saved devices, FM-Link,
RECONNECT, the page, the SMC-Mixer layout), and the room comes from code first: "shrink code and remove unnecessary
things first, like debug code, console commands, bluetooth icons". `FELUCCA_BLE=1 ./build.sh` builds it: the package
and build/skiff-samples-1.4.bin beside it.

| step | image B (slot free) | saved B |
| --- | ---: | ---: |
| the pack, the SMC-Mixer layout in (below) | 582,748 (-1,184) | |
| a. the BLE unit built for size (-Oz) | 582,196 (-632) | 552 |
| b. no console words or diagnostics (`BLE_CONSOLE=0`: layer 1's `ble`, the core's `ble` / `fml`) | | ~5,850 |
| c. no top-bar Bluetooth icon (the page's state line says it) | | ~240 |
| **a + b + c: the Bluetooth build** | **576,104 (5,460)** | 6,644 |

RAM 93,772 of 98,304 B, pool 345,232 of 356,352 B. (b and c measured together: 6,092 B; c is the header's icon
drawing and bt_head_icon, from the symbol sizes.) Without the console the serial logger (tools/fm1_serial_log.py)
reads no Bluetooth state: `BLE_CONSOLE=1 FELUCCA_BLE=1 ./build.sh` brings it back for a device test (it then does not
fit, by about 400 B by the estimate above, unless something else goes).

- The pack: tools/gen_samples.py `FELUCCA_SMP_PACK` (the plain build's sample bytes exactly, then the zone and SLICE
  tables), installed over USB: editor protocol PACK (90, web/EDITOR_PROTOCOL.md), the Bluetooth site's INSTALL
  BUILT-IN SAMPLES step (web/pack_step.js, web/make_site.py --pack), tools/fm1_sample_pack.py. Without it (or with
  another firmware's) the built-in sounds play a sine, say NO SAMPLE, and once SAMPLES: INSTALL PACK.

## The pack with the SMC-Mixer layout, before the code shrank (2026-10-24): 1,184 B over the slot

The user's design: one user slot (USR1), the built-in samples (PIANO, FLUTE, SAX, SLICE's BREAK and FUNK) as a pack in
USR2 and USR3's flash, installed over USB (editor protocol PACK 90, web/EDITOR_PROTOCOL.md; the Bluetooth site's
INSTALL BUILT-IN SAMPLES step, web/pack_step.js with web/make_site.py --pack; tools/fm1_sample_pack.py). Measured with the
SMC-Mixer layout still in (tests/ble_pack_test.c, tests/site_pack_test.py).

| | image B (slot free) | `.data + .bss` B | pool B |
| --- | ---: | ---: | ---: |
| the pack with the sample bytes only, the tables in the image | 585,980 (-4,416) | 93,824 | 345,232 |
| **the pack with the zone and SLICE tables too (as built)** | **582,748 (-1,184)** | 93,824 | 345,232 |
| .. and the BLE unit at -Oz (measured, not kept: untested code) | 582,196 (-632) | | |

The samples leave the image, but the code that plays them comes back: the build without them had every built-in
zone empty and a one-byte SMP_DATA, and the compiler dropped the sample, grain and slice readers' paths for them
(about 4.5 KB); the pack's reading, checking and install code is about 1 KB.
