# PS3 Tools Kit

Free, open-source homebrew toolbox for the PlayStation 3, created by **North**.
Interface in **French, English and Russian**. Current version: **2.2**.

Written in C with PSL1GHT; the interface is drawn with Cairo and FreeType (Inter font).
It brings together system information, hardware tests, file tools, read-only dumps,
developer tools, a technical lab and a catalogue of PKGs you can download.

---

## ⚠️ Read this before using it: bugs and slowness

**The software often glitches in the menus and on the console because the PS3 does not have enough processing power.**

The whole interface is drawn in software by the console's CPU (Cairo), not by the RSX.
On the PS3 this results, depending on the screen, in:

- slow or choppy menus, with navigation that responds late;
- display errors, or screens that freeze for a moment;
- some screens being heavier than others (Dev Kit, RSX scenes, graphs, long lists).

Other things to know:

- The app was developed and tested mostly in **RPCS3** and on a PC simulator. Testing on a real console is still very limited: **do not treat any result as validated on hardware**.
- Press **Select** to show or hide the smoothness readout (milliseconds per frame, frames per second, render time, output resolution). Version 2.2 outputs at 720p when the display allows it, to lighten rendering.
- If the app freezes, quit with the PS button (Quit) and relaunch it. The read-only tools change nothing if they hang.
- **The Modifications tab of "Dumps Patch" can brick your console** (see below). Only use it with a verified flash backup.

---

## Contents

1. [Main menu](#main-menu)
2. [System](#system)
3. [Dumps Patch](#dumps-patch)
4. [Developer](#developer)
5. [Dev Kit](#dev-kit)
6. [Workshop Tools](#workshop-tools)
7. [Settings](#settings)
8. [What the app writes](#what-the-app-writes)
9. [Known limitations](#known-limitations)
10. [Building](#building)
11. [Credits](#credits)

---

## Main menu

Six sections: **System**, **Dumps Patch**, **Developer**, **Dev Kit**, **Workshop Tools**, **Settings**.
Left/Right to choose, Cross to open, Circle to go back. The bottom of the screen shows the CPU and RSX temperatures and the free hard-drive space.

---

## System

Basic tools, read-only.

- **System info**: two pages.
  - *Specifications*: the manufacturer's theoretical values (not measured).
  - *Measured and detected*: integer compute throughput, memory copy, frame time, temperatures, and detected capabilities (CFW or HEN, LV1/LV2 access, NAND/NOR flash, USB, controllers, Blu-ray…). Real MHz cannot be read: these are throughputs to compare between consoles or before/after cleaning.
- **Files**: read-only explorer. Square = tools: SHA-256 hash, folder size, search by name, hex viewer (go to an offset, search a hex sequence or text, 8/16/32-bit values in big/little-endian), mark a file then compare it with another (byte by byte, offsets of the differing areas, SHA-256).
- **Controller test**: buttons, sticks, L2/R2 pressure, Sixaxis.
- **Save backups**: hard drive → USB stick and USB stick → hard drive (`/dev_hdd0/home/<user>/savedata` ↔ `/dev_usb000/PS3/SAVEDATA`). A restore never overwrites a save that is already present. Copies go through a `.partial` folder: each file is read back and compared (size + CRC32), and the folder is renamed only if everything is valid.
- **Network**: connection state, IP address, DNS and web connection test.
- **Screen test**: solid colours to spot dead pixels.
- **System analysis** (CFW or PS3HEN): read-only, changes nothing.
- **Diagnostics**: 13 checks (firmware, processor, memory, sensors, disk, controller, network, USB, optical…). Each result is "passed", "failed" or "not tested".
- **Stress test**: CPU load, memory (patterns verified) and RSX display, from 30 s to 10 min, automatic stop at 88 °C (CPU) or 85 °C (RSX), error log.
- **Storage**: space map (largest folders) and a speed test with a temporary file that is read back, verified, then deleted.
- **Devices**: 8 USB ports live (plugged / unplugged), recognised content, transfer test.
- **Optical drive**: disc type, title and version (PARAM.SFO), read test, errors.
- **Audio**: left / right / alternating tones, sweep, sync beeps with a visual check.
- **Thermal monitor**: CPU / RSX curves (up to 30 min), idle versus load, CSV export.
- **Report**: console sheet, repairer's notes, before / after comparison, text export. No IDPS, PSID or serial number is written.

---

## Dumps Patch

### Dump tools (read-only)

Syscon Error Log, AIM, VTRM, LV1, LV2, Flash PTAB, EEID, NAND/NOR, SRK/SRH, Sysrom, applicable version, token seed, temperatures, and **Compare two dumps** (report of differing areas, without showing any data).

- Files go to `PS3TK_DUMPS` on the USB stick or the hard drive (your choice, Triangle).
- Each dump gets its own **dated folder** (`YYYY-MM-DD_HHMMSS_<tool>`): an older dump is never overwritten.
- Written as **`.partial`**, fully read back, then renamed. A `.partial` is an interrupted or invalid dump.
- **SHA-256** of every file in `SHA256SUMS.txt` (checkable on a PC with `sha256sum -c`).
- NAND/NOR dump with **3 reads**: the second read is compared to the first, then the saved file is read back (per-block CRC and SHA-256). `flash_info.txt` states `Verified: YES/NO`.
- The **full 16-byte IDPS** is compared with the console's for EEID and flash; the IDPS itself is never written to a file.

⚠️ **Dumps contain data specific to your console (IDPS, keys). Never share them.**

### Modifications (write)

Locked tab (hold L2 + R2 for 3 s): Reboot, Recover Mode Toggle, RSOD Fix, Flash EEID, Flash NOR.
Each tool checks everything without writing anything, then asks for confirmation.

- Flash EEID / Flash NOR: file in `PS3TK_DUMPS/A_FLASHER/`, which must come from the **same console** (header, 16-byte IDPS and bootldr compared).
- A backup is accepted only if its **SHA-256 is recomputed** and equal to the recorded one.
- The current area is backed up, **fully read back and compared** before any write; at the slightest difference, nothing is written.
- Only the blocks that change are written, each block is read back, then the **whole area is verified**. On failure: rollback, full read-back and, if that fails, a `RECUPERATION.txt` file (FR/EN) with the steps to follow.
- No patch is applied to LV1 memory.

⚠️ **These tools can brick a console.** Only use them with a verified flash dump (PyPS3checker) and a USB stick holding your CFW's PUP. The author is not responsible for any damage.

---

## Developer

9 tools to inspect, test and debug homebrews. Read-only, except "API tests" (a small temporary file in `/dev_hdd0/tmp`, deleted afterwards) and the exports you request with Start.

- **SELF / ELF inspector**: SELF header (type, attributes, fSELF or signed), inner ELF (architecture, entry point, segments, sections), encrypted segments flagged. Fields read from psdevwiki, not validated on a console.
- **PARAM.SFO inspector**: all parameters, Title ID, version, category, declared minimum firmware.
- **Hex viewer** and **Comparator**: the "Files" tools, opened directly with a file picker.
- **Log console**: the app's journal or any text file (last 2000 lines), "errors only" filter, export.
- **API tests**: files, network, controllers, audio, display, system, with parameters, result and code for each test.
- **Performance**: frame time, heap memory, operation durations, reading of `/dev_hdd0/tmp/ps3tk_perf.txt` (lines `name;value;unit` that a homebrew can write).
- **Environment information**.
- **Debug report**: creates a `DEBUG_<date>` folder (environment, journal, errors, API, performance) without IDPS, PSID, keys or dumps.

---

## Dev Kit

Technical lab with 10 tiles. **Nothing has been validated on a real console.**

Stated limits: no SPU (no SPU toolchain available), no real RSX shader. The Cell lab uses PPU threads only; the "RSX" scenes are pseudo-3D drawn in software by Cairo. These tests therefore do not measure the real performance of the RSX or the SPUs.

Read-only, except temporary files in `/dev_hdd0/tmp/PS3TK_LAB` (always cleaned up) and the exports you request.

- **RSX lab**: 6 scenes (textures, lighting, transparency, depth, full-screen effect, fill), average and worst frame time.
- **Cell lab**: integer workloads on 3 threads compared with the expected result, exact sum, lock-protected counters, strict alternation between 2 threads.
- **Memory tests**: 6 patterns (0x00, 0xFF, 0x55, 0xAA, walking bit, address) written then read back in the app's memory, with throughput.
- **File test bench**: sequential read/write, 4 KB random access, 100 small files, data verification.
- **Audio lab**: tones, channels, audio/video sync pattern, return to the Dev Kit.
- **Controller lab**: raw values, pressures, sensors, vibration, up to 4 controllers shown together.
- **Automatic scenarios**: chains the chosen tests, adjustable duration and repetitions, list of failures, session saved.
- **Session comparator**: compares two sessions (Before, After, Config A, Config B), differences in %, under 3% = measurement noise.
- **Technical console**: journal of operations with durations and error codes, error filter, export.
- **Lab profiles**: "Quick test", "Extended stability", "Storage", "Graphics", plus 4 personal slots.

Settings are kept in `lab_profiles.txt` and `lab_sessions.txt` (app folder).

---

## Workshop Tools

A catalogue of homebrews: tick one or more tools, the app downloads their PKGs into `/dev_hdd0/packages`, then you install them from the XMB (Game > Install Package Files).

- **Direct installation is not implemented**: it depends on the CFW/HEN and is unverified.
- **Catalogue**: a text file, one line per tool: `id|name|version|size|sha256|compat|url|description`. The address is entered with Start (stored in `atelier.txt`). With no address, the `atelier_catalogue.txt` file in the app folder is read.
- **PC server**: `atelier/serve_catalog.py` serves a folder of PKGs, generates the catalogue and the SHA-256 values, and supports resuming.
- **Download**: HTTP only (no HTTPS), resume through a `.part` file, progress and speed, cancel with Triangle.
- **Checks**: free space, size, PKG header, SHA-256 if provided. An invalid file is deleted.
- Only install PKGs whose source you know: the app does not analyse their content.

---

## Settings

Test journal, language choice (French, English, Russian) and about.

---

## What the app writes

Outside the Modifications tab, the app **never** writes to `dev_flash` or to the flash.
It writes only:

- its settings and journal in its `USRDIR` folder;
- the exports you request (USB stick if present, otherwise `/dev_hdd0/PS3TK_EXPORT`);
- the saves you choose to copy and the dumps;
- small temporary files deleted afterwards (`/dev_hdd0/tmp`, `PS3TK_LAB`);
- downloaded PKGs in `/dev_hdd0/packages`.

---

## Known limitations

- Slowness, choppiness and display bugs in the menus on the console (see the start of this document).
- Little testing on a real console; the rest comes from RPCS3 and a PC simulator.
- No SPU and no real RSX shader in the Dev Kit.
- No HTTPS in Workshop Tools.
- Some system behaviours were not verified on hardware (file renaming, network receive timeout, SELF fields).
- The real CPU and RSX MHz cannot be read.

---

## Building

PS3 toolchain (ps3dev / PSL1GHT) with Cairo, FreeType and pixman in the portlibs:

```
make pkg
```

The packaging tools can be replaced:

```
make pkg MAKE_SELF_NPDRM=... PYTHON=python3 SFO_PY=... PKG_PY=...
```

---

## Credits

Created by **North**. Inter font, © The Inter Project Authors, under the SIL Open Font License 1.1
(see `pkgfiles/USRDIR/FONT-LICENSE-OFL.txt`). Cairo, FreeType, PSL1GHT.

No warranty. The author is not responsible for any damage caused to your console.
