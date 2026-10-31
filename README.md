# Dreamy_bacon's LSDJ Burrito

LSDJ Burrito runs [Little Sound Dj](https://www.littlesounddj.com) on the M-VAVE FM-1, played from the FM-1's own keys and knobs, with songs saved to the FM-1's flash. It adds per-channel effects (filters including notch and 24 dB low-pass, drive with soft / hard / fold / fuzz, warmth, lo-fi, an LFO to cutoff, volume or pan), a master echo and compressor, MIDI clock in and out, MIDI notes and CCs, themes, and experimental Bluetooth MIDI.

- **Web page:** [dreamsofbacon.github.io/lsdj-burrito](https://dreamsofbacon.github.io/lsdj-burrito/): install or update LSDJ Burrito step by step with your own `lsdj.gb` (LSDj is not included, and your ROM never leaves your computer), then control effects, songs, colours and settings over USB (or Bluetooth).
- **Status:** alpha (FM-1_995, package identity FM-1_5370995). Installing from the web page with your own LSDj is experimental; LSDj 9.3.9 is the verified version.
- **Based on** [GameDude](https://github.com/ChanceTheMaker/gamedude-fm1-smk) by ChanceTheMaker (GPL-3.0). No Bluetooth (removed in FM-1_994, with GameDude's game library loader and website). The firmware README follows below.

Not affiliated with M-VAVE, Johan Kotlinski (LSDj) or Nintendo.

---

# LSDJ Burrito firmware (based on GameDude for FM-1 / SMK37)

## LSDj Burrito: note mode sets LSDj's SYNC to KEYBD (FM-1_995)

- The FM-1's keys (note mode) and incoming MIDI notes reach LSDj as a PS/2 keyboard on the link
  port, which LSDj only reads with PROJECT > SYNC = KEYBD. With LSDj 9.3.9 and SYNC OFF, the FM-1
  now sets it by itself when you press HOME for note mode, play a note or send a MIDI note, and
  says LSDJ SYNC SET / TO KEYBD FOR NOTES. It does what LSDj's PROJECT screen does
  (harness/keybd_auto.sh): the song's SYNC byte, LSDj's working copy (WRAM CB66), the interrupt
  bits LSDj builds IE from (CB6D) and IE's serial bit at once (gp_ie_or). SYNC MIDI and LSDJ are
  left alone (MIDI clock in and out need them); note mode then says SET LSDJ SYNC=KEYBD, as on
  other LSDj versions. The setting is part of the song, so SAVE keeps it.

## LSDj Burrito: no Bluetooth (FM-1_994)

- **Bluetooth is gone**: LSDJ Burrito's Bluetooth MIDI (alpha 56; an opt-in build since alpha 60,
  as it only fitted with the small, crunchy sound buffer, and it linked JieLi's binary-only
  Bluetooth libraries) and GameDude's inherited game library loader (Bluetooth and USB), with
  its website, tools and tests. The settings lose BLUETOOTH; MIDI CLOCK IN loses BT and CLOCK OUT
  BT / USB+BT (saved values read as ALL, OFF and USB); link op 0x21 (Bluetooth status) answers
  status 1. Recovery (Felucca's USB update loader) is untouched.
- FM-1_993 was a private Bluetooth build only, so this is FM-1_994 (FM-1_5370994).

## LSDj Burrito: its own firmware number (FM-1_992)

- **Package identity FM-1_5370992** (FM-1_ + our prefix 537 + a 4-digit version; tools/build.py
  LSDJ_SLOT_PRODUCT). The FM-1_9NN numbers used up to FM-1_991 are shared with other firmwares on
  The Firmware Hub, so updaters could not tell them apart. Felucca's updater reports it as is (up to
  27 characters); the web installer takes FM-1_ + 3 to 15 digits and knows FM-1_537 + 4 digits as
  LSDJ Burrito. The short name FM-1_992 (LSDJ_VERSION, CART_SLOT_VERSION, BURRITO_VERSION) stays in
  the web page's link INFO (8 bytes) and on the splash; template.json carries both (product, version).

## LSDj Burrito: every sample kit fits (FM-1_991)

- **Kits are stored without their empty filler.** A kit often uses only part of its 16 KiB bank;
  the rest is 0xFF. Slot version 2 (src/lsdj_slot.h) stores each bank only up to its last
  non-0xFF byte, with a table of start and length per bank; gb_port.c's rom_read returns 0xFF past
  a bank's length, so LSDj reads exactly the bytes it did before (same per-read cost: one table
  lookup and a compare). The slot is now every byte left in the app area (503296 bytes here, was
  30 whole banks / 491776). A stock LSDj 9.3.9 (13 program banks, 21 kits) needs 464269 bytes:
  all 21 kits fit (17 before), 39 KB to spare. Song saves are untouched (128 KiB save RAM, its own
  flash store).
- Slot version 1 (FM-1_990 and older) is still read: by the firmware and by the web page when it
  reads an installed LSDj back for an update.

## LSDj Burrito: the FM-1 pushes its screen to the web page (FM-1_990)

- **The web page's screen mirror keeps up at SCREEN RATE 30.** Up to 989 the page asked for each
  picture (SCREEN_SYNC) and waited for its rows before asking again; that round trip made it skip
  pictures at 30 a second. With force bit 31 (LSDJ_LINK_PUSH) on SCREEN_SYNC the FM-1 now sends each
  new picture by itself for 3 s: a SCREEN_FRAME (op 0x29: pictures shown, the bands that changed,
  their hashes), then those bands' rows, after the previous picture's rows are out (screen_push in
  board.c, lsdj_link_push_* in lsdj_link.h). The page renews it every second, and sooner after a
  loss: the SYNC carries the hashes the page holds, so lost bands come again. USB only; Bluetooth
  and older firmware as before.

## LSDj Burrito: SCREEN RATE setting (FM-1_989)

- **SCREEN RATE (SETTINGS / DISPLAY, under CPU METER): 10 / 15 / 20 / 30 pictures a second.**
  The FM-1 drew every 6th Game Boy frame (10 a second) on both paths; the web page's mirror
  now keeps up with that, so this sets both. More pictures cost CPU time next to the sound:
  watch the CPU meter (U = underruns) and step back if the sound clicks. Default 10/S, kept in
  the FX+LIGHTS word (bits 18-19, 0 = 10/S, so older words read as before). The web page's
  Settings tab shows it under CPU meter.

## LSDj Burrito: screen mirror rows back to back (FM-1_988)

- **The web page's screen mirror keeps up with the FM-1's own screen.** 985-987 sent one
  SCREEN_PIXELS row per main-loop pass, and only once the SysEx ring was empty, so the length of
  a pass (scaling a picture, drawing panels) set the rate: Colin saw 1-9 pictures a second on
  the page while playing. screen_stream now sends up to 12 rows a pass, each only if it fits the
  ring whole (ota_wire_send never waits), keeping at most 24 packets queued; while MIDI out
  waits it lets the ring empty first, so notes and clock still go out within ~4 ms.
- The page shows each finished round at once (987 held changing rounds up to 50 ms) and shows
  the FM-1's own pictures a second (SCREEN_SYNC's count) next to its own.

## LSDj Burrito: screen mirror without torn panels, 30 a second (FM-1_987)

- **The web page's screen mirror no longer shows torn or missing panels.** FM-1_983-986
  read the emulator's picture (gp_pixels), which CPU1 starts drawing the next frame into as
  soon as the panels are scaled out, so rows sent later could lack the FX panel or menu.
  The mirror now reads the LCD image (`screen`, written by the main loop only right before
  display_frame, the loop the mirror runs in): lsdj_link_px reads picture pixel (x, y) back
  from it (row ceil(3y/2), columns A,A,B, bytes swapped). No RAM added; the page's protocol
  is unchanged. harness/mirror_tear_emu.mjs: with panels up the old source differed from
  the LCD in about 1 sample in 15 (up to 121 rows), the LCD image never.
- The page looks for changes every 33 ms (was 50) over USB: 30 a second.

## LSDj Burrito: cursor FX on other LSDj versions (FM-1_986)

- **The effects follow LSDj's cursor on more LSDj versions.** The web page recognises a
  program by its code banks (0-7, 1B-1F) with the palette colours (bank 1) and palette
  names (bank 1B) blanked, so custom palettes and kits (blank kit slots too) keep the
  match. LSDj 9.3.9 that way: every helper (slot byte 148 = 1). LSDj 9.4.0, 9.2.L, 9.1.C,
  8.4.9, 8.4.2 and 4.6.2: slot byte 148 = 2 with the cursor and tempo bytes at 149 and
  151 (found with harness/find_cursor.c and find_tempo.c), so cursor-following effects
  and the LFO tempo work; SEQ live mode, tempo editing, cues and MIDI sync still need
  LSDj 9.3.9 (src/lsdj_slot.h, cursor_detect in firmware/board.c).

## LSDj Burrito: faster screen mirror (FM-1_985)

- **The web page's screen mirror is several times faster.** One request (0x27
  SCREEN_SYNC) carries the hashes the page holds; the FM-1 answers with its own and
  then sends the changed rows by itself (0x28 SCREEN_PIXELS), packed 7 bits to a byte
  with 0-4 bits a pixel, instead of one request per row. Rows go from the main loop,
  one at a time once the USB SysEx queue is empty. USB only; over Bluetooth (and with
  FM-1_983/984) the page reads rows one by one as before. No RAM added.

## LSDj Burrito: two effects areas (FM-1_984)

- **Channel effects and master effects are apart.** Turning KNOB 1-4 or SELECT pops up
  the channel pages (FILTER, DRIVE, LFO, LO-FI) for the channel under the cursor, for a
  moment, as before. The **FX button** opens the master pages (MASTER, ECHO, ECHO MOD;
  C4-F4 mute) and keeps them open until FX is pressed again.

## LSDj Burrito: screen mirror on the web page (FM-1_983)

- **The web page shows the FM-1's screen** (above Effects): the 160x144 picture the LCD
  shows, panels included, read over the web link (ops 0x25 SCREEN_INFO, 0x26 SCREEN_ROW,
  `firmware/lsdj_link.h`). The page asks only for the 8-row bands whose hash changed,
  so a cursor move costs a few rows. View only; nothing is read unless the page asks,
  and no RAM is added (rows are read from the emulator's picture).

## LSDj Burrito: master pages apart from the channel pages, autosave (FM-1_982)

- **FILTER K2 is the echo send**, next to K1 Channel FX; the filter type moved to
  LO-FI K3. Save words, link and MIDI CCs are unchanged (the knob layout is not in
  the saved words: the webpage picks it from the firmware number).
- **SELECT stops** at the first and last FX page, so turning back always reaches
  FILTER (PAGE WRAP brings back going round).
- **A muted channel's panel is grey** (still adjustable), and turning one of its
  effects puts why on the title row for the POPUP TIME: `PU1 MUTED: KEYS` / `WEB` /
  `CC81`.
- **MIDI CC to effects shows** as a message (`PU1 CUTOFF` / `MIDI CC 74: 40`);
  **CC OUT** sends each turned FX knob's CC over USB (channels as CC FX, 1 if off).
- **KNOBS: CHANNELS**: SELECT picks one per-channel effect (14, in page order, then
  ECHO and ECHO MOD as pages), K1-K4 set it on PU1 PU2 WAV NOI; muted columns grey.
- **PAGE LIGHTS**: a soft black key (C#4 D#4 F#4 G#4 A#4 C#5, none a Game Boy
  button) shows FX page 1-6, or LSDj 9.3.9's SONG CHAIN PHRASE INSTR TABLE GROOVE.
- **MODE LIGHTS**: HOME's note / button change plays the power-on sweep once every
  key and button is let go.
- **BATTERY**: three cells (Felucca's thresholds) or USB after "FX" in the screen
  map and on the settings list; BATTERY LOW once. LIVE carries it (a byte after the
  effects words; older pages ignore it).
- **FX THEME**: the FX panel, lists and messages in a theme of their own, or MATCH
  (default) LSDj's.
- All switches: SETTINGS / FX+LIGHTS (`firmware/lsdj_feat.h`, save-store record 23,
  link ops 0x19 / 0x1A). The overlay font is packed bytes now (same pixels, 1.2 KB less).

## LSDj Burrito: echo back with the channel pages (FM-1_978)

ECHO and ECHO MOD are back in the tap-FX / SELECT pages (1/6..6/6, after FILTER,
DRIVE, LFO, LO-FI); holding FX shows only MASTER (All FX, compressor) and the
channel strip. The echo itself is unchanged: one echo shared by every channel, each
channel with its own send (LO-FI K3). Saved setups on ECHO / ECHO MOD keep that page.

## LSDj Burrito: hold FX for the master page (FM-1_977)

The effects pages are split by what they act on:

- **Tap FX** (or turn SELECT): the channel pages FILTER, DRIVE, LFO, LO-FI
  (1/4..4/4), on the channel under LSDj's cursor.
- **Hold FX**: the master pages MASTER, ECHO, ECHO MOD (1/3..3/3), shared by
  every channel. SELECT turns them and K1-K4 play them while FX is held; the
  page closes when FX is let go. A channel strip (row 14) shows PU1 PU2 WAV NOI,
  lit while playing; C4 D4 E4 F4 mute / unmute them, and their key lights are
  lit while the channel plays. These are the same mutes as MIDI CC 81 and the
  webpage's channel buttons (link op 0x24 MUTE_SET, live only, never saved).
  The old hold shortcuts G4-C5 (channel FX) and D5 (freeze) are gone: channel FX
  is FILTER K1 and freeze is ECHO MOD K4.

A quick tap (under 250 ms, nothing turned) never shows the master page. Saved
setups that were left on a master page open on FILTER.

## LSDj Burrito: SEQ and the other LSDj helpers in web builds (FM-1_976)

Web-installed (generic template) builds used only the cursor address from the
verified LSDj 9.3.9 profile, so SEQ (live / song mode), EDIT tempo, ARP song
cues, MIDI clock / KEYBD sync and the PLAY beat light did nothing. The profile
is set by the browser only when the program banks' SHA-256 matches the verified
9.3.9 program, and every one of these addresses belongs to that program, so
FM-1_976 enables all of them for profile 1. Unknown programs still get none.

## LSDj Burrito: master FX controls (FM-1_975)

The first FX page is **MASTER**: K1 switches **All FX** on/off; K2 is the
shared compressor. Turning All FX off produces the original dry mix, including
channel mutes. Channel effects, echo and compressor settings are retained;
their internal state keeps advancing so re-enabling does not resume a stale
echo or LFO. Bypass is not a CPU-saving mode.

| Page | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- |
| 1 MASTER | All FX | Compressor | - | - |
| 2 FILTER | Channel FX | Filter type (979: Echo send) | Cutoff | Resonance |
| 3 DRIVE | Drive | Distortion | Warmth | - |
| 4 LFO | Rate | Depth | Shape | Destination |
| 5 LO-FI | Crush | Decimate | Echo send (979: Filter type) | - |
| 6 ECHO | Time | Feedback | Mix | Tone |
| 7 ECHO MOD | Wobble | Sync | Mode | Freeze |

SELECT changes pages; the screen map uses the same numbers across two rows.
Unused knobs are blank/inert. Existing FX-held mute, channel bypass and freeze
shortcuts are unchanged. Accent bars and the no-channel-tint styling remain.
The webpage mirrors these pages on supporting firmware, retaining the original
layout on older firmware and explicitly explaining the unavailable master switch.

The 17-word FX format uses byte 37 = 2 for this layout and byte 39 for global
bypass (0 on, 1 off). SAVE/Keep and settings backups preserve the switch.
Legacy saved setups default to All FX on and migrate page indices. Older live
clients preserve effects they cannot control, including global bypass.
The unresolved physical cursor-following report is separate; this reorganization
does not claim to fix it.

## LSDj Burrito: alpha 71 effects display

The on-device effects panel keeps LSDj's font, palette, four knob columns,
active-knob readout, channel text and bottom labels. Amount bars now have eight
segments: each spans 16 raw setting values (`10` hex), with partial fill between
boundaries. The shared `00`, `20`, `40`, `60`, `7F` ruler matches the hex setting
under each amount bar; `00` is empty and `7F` is full. Screen pixels quantize the
fill, so the numeric value remains the precise reading.

Alpha 71 fills the bars with the selected theme's accent colour instead of
LSDj's muted value-field shade. Classic uses amber; other presets and CUSTOM
use their own accent. The channel-column tint has been removed; channel identity
remains in the text, and every bar has the same normal background.
Empty markers, segment gaps, text and choice selectors keep their existing
styling. Custom themes should use an accent distinct from their background.
Rendering tests check accent/background separation across all preset entries,
all four background shades and identical rendering for every channel selection.

Filter types, distortion modes, LFO shapes/destinations and on/off modes appear
as choice lists, with only the selected name inverted. Their two-letter codes
match the value row; turning the knob shows the full name in the top readout.
The amount scale is a **raw setting**, not Hz, milliseconds or a linear measure
of audible intensity. Echo time and LFO rate retain their actual time/note/rate
readouts when active, including in synced modes.

Cue/tempo panels keep their previous display. Knob behavior, DSP, audio buffers
and saved-effect formats are unchanged. `tests/lsdj_native.c` checks all 128
amount values, segment boundaries, all 2-6-choice selection positions and panel
bounds; `tests/lsdj_fx_ui.c` checks matching selector/value labels and raw scales.
The refreshed FM-1_974 webpage template includes these display changes and the
CPU1 save fixes, but retains its generic-ROM limitations. Physical alpha 71
and template-combination confirmation are separate from host/emulator checks.

## LSDj Burrito: experimental webpage template

This fork also builds the ROM-free FM-1_995 (package identity FM-1_5370995) LSDj template for the sibling Burrito
website.
Local ROM builds (never published) keep FM-1_9NN in firmware/fm1_recovery.h.

From this repository in WSL:

```sh
python3 tools/lsdj_template.py --toolchain /root/.jieli/toolchain --sdk /root/ac79-sdk
```

Output is `build/lsdj-template/fm1-lsdj-template.fwsc` and `template.json`;
`--output-dir` overrides the destination. The build also replaces intermediate
`build/fm1` files. SDK hashes, image bounds, erased slot, package round trip and
simulated OTA are checked. Only the ROM-free package and matching metadata may
be copied to the sibling site's `lsdj/` directory. Never publish ROM-bearing builds.

The current template has 31 stored 16 KiB banks plus a 256-byte header. The browser
requires explicit kit selection if the ROM exceeds this budget and preserves
logical bank numbers. Unrecognized program/data banks cannot be discarded.
The dynamic slot parser validates header/data CRCs and a complete sequential bank
map before emulation. The browser enables the read-only cursor profile only
when the program SHA-256 matches known LSDj 9.3.9 code; custom or omitted sample
kits do not change this fingerprint. Slot byte 148 selects that profile under
the header CRC. Cursor-following FX is restored without the channel tint.
Unknown programs explicitly fall back to ALL channels, never title-based
address guessing. Tempo shortcuts, MIDI clock integration and cues remain
disabled in generic builds. Back up saves before switching versions.

FM-1_974 adds read-only installed-cartridge export for future updates without
choosing the original ROM again. The link's `CART_INFO` (0x22) reports slot
length/header CRC/data CRC; `CART_READ` (0x23) returns the requested offset and
at most 120 bytes, within the validated immutable slot only. No arbitrary
addresses, saves, writes, pause or flash operations are exposed. Save imports
block these reads. Non-slot builds and older firmware report unsupported.

The page verifies chunks, both CRCs and slot layout, reconstructs missing banks
as FF, and inserts that ROM into the latest verified template. All installed
kits stay selected and locked; if they cannot fit, preparation is refused.
The original-ROM path remains available for explicit kit changes. Previously
omitted samples cannot be recovered. Readback neither backs up songs/settings
nor installs firmware; installation is a separate explicit action.

The first readback-capable firmware must be installed once using a private ROM.
974 is that bootstrap release; no physical readback confirmation has been
reported. `tests/lsdj_link.c` checks bounds, retries, unavailable slots and
read-only state; `tests/lsdj_cartridge_link.c` exposes the real C protocol over
stdin/stdout for the site's `tests/device-update.mjs` integration tests.

This template is **experimental, not hardware-validated**. Packaging and simulated
OTA success do not establish boot/audio correctness or recoverability after
interrupted flashing. `tests/lsdj_slot.c` tests slot parsing; `tests/lsdj_rom.c`
accepts a private browser-generated slot for a 600-frame host emulator smoke test
and, with a recognized cursor profile plus a private 128-KiB SONG-screen save
as the second argument, checks all four real ROM cursor positions and
channel-isolated cutoff changes.
The sibling site's `tests/loader.mjs` tests browser packing and optional local ROMs.

## Based on GameDude

This firmware started from GameDude, a Game Boy player for the M-VAVE FM-1 and SMK37
(https://github.com/ChanceTheMaker/gamedude-fm1-smk). LSDJ Burrito keeps its emulator core,
FM-1 / SMK37 board code, CPU1 work and SMK updater. FM-1_994 removed what LSDJ Burrito does not
use: GameDude's game library loader (Bluetooth and USB), its website and browser player
(`web/`), the Windows SMK USB helper, the bundled homebrew and their tools and tests, and
LSDJ Burrito's own Bluetooth MIDI (alpha 56; it only fitted with a crunchy sound buffer, and it
needed JieLi's binary-only Bluetooth libraries).

## Run and build

Native firmware needs WSL and the JieLi pi32v2 toolchain (`JIELI_TOOLCHAIN` overrides the path):

```sh
python3 tools/lsdj_template.py --toolchain /path/to/jieli/toolchain --sdk /path/to/ac79-sdk   # the website's template
python3 tools/build.py fm1 --rom /path/to/lsdj.gb --kits 3                                  # a private build (never publish)
python3 tools/build.py smk37 --toolchain /path/to/jieli/toolchain
python3 tools/build.py test --zig /path/to/zig
```

Private cartridge builds stay under ignored `build/`; never publish them.

## Sources and licenses

GPL-3.0-only integration with retained third-party notices. See [LICENSE](LICENSE), [upstream notices](vendor/felucca/LICENSING.md), and [source provenance](vendor/provenance.json).

- Peanut-GB and MiniGB APU: Mahyar Koshkouei, Alex Baines and credited contributors including Lior Halphon; MIT. Pinned Peanut-GB revision `d0bcca771c83a2638c93a9ae61f3d51f226dc905`.
- Felucca: Leo Kuroshita and Huegelton Instruments; GPL-3.0-only. SMK/MusicCoach adaptations by Chance Roth / ChanceTheMaker. Retained file hashes record the imported local reference sources.
- SMK updater (vendor/smk-updater): retained from ChanceTheMaker/sloop-smk, with original copyright notices and hashes in provenance.
- Three unmodified JieLi AC79 SDK package files: JieLi Technology; Apache-2.0. License and attribution accompany the public template.
- Original GameDude pixel logo, startup animation and diagnostic cartridge are project assets. Game Boy and Nintendo are trademarks of Nintendo. This independent project is not affiliated with Nintendo.

Dual-core hardware trial: FM-1_934 was installed and verified responding over USB. CPU1 now completes immutable-frame scaling while CPU0 retains GB/APU emulation, audio interrupts, controls and USB. Initial hardware samples confirmed core ID 1, an increasing completed-job count, and no CPU1 faults or fallbacks. This corrects the earlier assumption that CPU1 never started: trial 933 reached its worker loop but fell back before accepting a frame. Build 934 fixes late-completion timeout handling and removes per-pixel division from the scaler. Host tests, FM-1/SMK builds and simulated install/restore pass. The owner confirmed normal picture, controls and sound. Longer stability testing remains pending. The v2.0 flasher serves build 938, retaining CPU1 scaling, the inner directional keys and grouped LED feedback on FM-1, with CPU1 also enabled on SMK. See [CPU1 trial notes](docs/CPU1_TRIAL.md).

## SMK flash compatibility release

The SMK update and stock-restore path now accepts the reported EB6014 flash
chip alongside 856014 and displays detailed loader failures. Update older
Windows USB-helper checkouts before installing this release.
See [release scope, evidence and recovery limits](docs/SMK_FLASH_COMPATIBILITY.md).
