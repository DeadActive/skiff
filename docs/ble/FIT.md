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

## It does not fit: open

- **Flash: 23,120 B over the slot**, with no margin. What is left to take, each the user's call: SLICE's FUNK
  (~16.6 KB with its slice table), USB audio's 48 kHz (`FELUCCA_UAC_48K=0`, its resampler's table 7.7 KB plus code),
  the device role (~6.3 KB), the SMC-Mixer layout (~1.7 KB).
- **Pool: 29,536 B short** (8 KB kept spare); the RAM has only ~100 B to give (it keeps 4 KB). The delay line
  (`fx.c` `dly_buf`, 131,072 B, 1.49 s) is a power of two: half of it (0.74 s, a quarter note down to 81 BPM) frees
  65,536 B, enough with room to spare.
