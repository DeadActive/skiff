// SPDX-License-Identifier: GPL-3.0-only
// BLE MIDI: 2026 DEADACTIVE
// The Bluetooth build's INSTALL BUILT-IN SAMPLES step (web/make_site.py --pack: added to its installer only). The
// pack (firmware/skiff-samples-VER.bin, tools/gen_samples.py FELUCCA_SMP_PACK) goes into the flash of user slots
// USR2 and USR3 over the editor protocol's PACK (90): INFO (the firmware's pack: length, id), BEGIN, WRITE, END (the
// header last). In the installer's module: $, log, sayK, lockInstaller, writing, Updater, IS_OTA, BackupConnection,
// bkPack, bkCrc, meta.
{
  const PACK_RC = { 1: "a bad request", 2: "not this firmware's pack", 3: "Stop the music on the FM-1 first",
                    4: "Flash write failed", 5: "The samples did not check: install them again" };
  const say = (s) => { $("status").textContent = s; log(s); };
  $("pack-go").disabled = !navigator.requestMIDIAccess;
  $("pack-go").addEventListener("click", async () => {
    if (writing) return;
    lockInstaller(true); $("pack-go").disabled = true;
    let link = null;
    try {
      const res = await fetch(meta.samples);
      if (!res.ok) throw new Error("The sample pack is missing from this site");
      const bytes = new Uint8Array(await res.arrayBuffer()), dv = new DataView(bytes.buffer);
      const len = bytes.length >= 256 ? dv.getUint32(8, true) : 0, id = bytes.length >= 256 ? dv.getUint32(16, true) : 0;
      if (bytes.length < 256 || dv.getUint32(0, true) !== 0x4B504B53 || dv.getUint32(4, true) !== 1 ||
          bytes.length !== 256 + len || bkCrc(bytes.subarray(256)) !== dv.getUint32(12, true))
        throw new Error("The sample pack file is damaged");
      let midi;
      try { midi = await navigator.requestMIDIAccess({ sysex: true }); } catch (e) { sayK("denied"); return; }
      const device = await new Updater(midi, log).find((d) => !IS_OTA(d));
      if (!device) { sayK("notfound"); return; }
      const input = device.link.input, output = device.link.output; device.link.close();
      link = new BackupConnection(input, output);
      let r;
      try { r = await link.request([90, [0]], { timeout: 2000 }); }
      catch (e) { if (e.code !== "timeout") throw e; say("This FM-1 does not run Skiff's Bluetooth build: install the firmware above first."); return; }
      const fwLen = r[2] | r[3] << 7 | r[4] << 14, fwId = (r[5] | r[6] << 7 | r[7] << 14 | r[8] << 21 | r[9] << 28) >>> 0;
      if (fwLen !== len || fwId !== id) { say("These samples are not the ones for the firmware on the FM-1: install this site's firmware first."); return; }
      if (!confirm("Install the built-in samples (PIANO, FLUTE, SAX, SLICE's BREAK and FUNK)?\n\n" +
                   "This replaces anything in the user sample slots USR2 and USR3: this build has one user slot, USR1. " +
                   "Stop the music on the FM-1 first.")) return;
      const ck = (a) => { const rc = a.at(-1); if (rc) throw new Error(PACK_RC[rc] || `PACK error ${rc}`); };
      ck(await link.request([90, [1]], { timeout: 4000 }));
      for (let off = 256; off < bytes.length; off += 256) {
        const chunk = bytes.subarray(off, off + 256);
        ck(await link.request([90, [2, off & 127, off >>> 7 & 127, off >>> 14 & 127, ...bkPack(chunk)]], { timeout: 4000 }));
        const pc = Math.round((off - 256 + chunk.length) / len * 100);
        $("bar").value = pc; $("status").textContent = `Installing the built-in samples: ${pc}%`;
      }
      ck(await link.request([90, [3, ...bkPack(bytes.subarray(0, 20))]], { timeout: 8000 }));
      say("The built-in samples are installed.");
    } catch (e) { say(`Built-in samples: ${e.message}`); log(String(e.stack || e)); }
    finally { if (link) link.close(); lockInstaller(false); $("pack-go").disabled = false; }
  });
}
