# Burnout 3 PS2 to Xbox save conversion feasibility

2026-10-22. Assessment for [discussion 59](https://github.com/xenophobentx/takedawn/discussions/59).

Update 2026-10-30: the importer is built (BO-323, toolkit
`src/kernel/kernel_ps2save.c`). It reads memory card images and the
single-save formats, works for US, PAL and Japanese saves, and builds the
Xbox file without a template. The rest of this page is the experiment as it
was on 2026-10-22.

A local proof of concept converted one PS2 folder save into an Xbox save
container. The result passed the checksum and signature checks and loaded in
the game. It was an experiment and made no claim about other saves.

## Inputs and verified layout

The public [HardLevel sample](https://hardlevel.com.br/download/burnout-3-widescreen-e-save-100/) contains `BASLUS-21050/BASLUS-21050`, `icon.sys`, `view.ico`, and a separate cheat file. Only the save data was used; no downloaded code was run. The Xbox reference was a local save copied into a temporary directory. (Later, in BO-323, the sample's data file turned out to be byte-identical to a PAL `BESLES-52585` save placed in a US folder.)

| Field | PS2 sample | Xbox reference |
|---|---|---|
| File size | 61,772 bytes (`0xF14C`) | 59,904 bytes (`0xEA00`) |
| Leading version word | `0x10` | `0x10` |
| First career profile | `0xE374` | `0xE374` |
| Career profile size | `0x49C` / 1180 bytes | `0x49C` / 1180 bytes, confirmed in game load/save code |
| Profile slots | Three, at `0xE374`, `0xE810`, `0xECAC` | One |
| Additive checksum | `0xF148`, over preceding little-endian 32-bit words | `0xE810`, over preceding little-endian 32-bit words |
| Xbox signature | None in this format | Last 20 bytes, starting `0xE9EC` |

Both original checksums were recomputed and matched. The Xbox reference's signature matched the nested zero-key HMAC that Takedawn's save importer uses.

The sample's first profile is named `Dwarf` and has 173 gold medal bytes. The other two slots are named `BURNOUT` and hold unplayed medal values. The binary contents and the Xbox load/save functions agree on the profile's position and size. Other regions and versions were not checked at the time.

## Proof of concept

The conversion script:

1. Checked the source sizes, version words and checksums.
2. Started from a copy of a valid Xbox container and its metadata.
3. Copied the PS2 header and records section up to `0xE334`.
4. Kept the Xbox settings bytes at `0xE334` through `0xE373`, so the PS2 controller, audio and video settings did not come across unchecked.
5. Copied the chosen PS2 career's 1180 bytes into the Xbox career slot.
6. Recalculated the Xbox checksum at `0xE810` and the signature at `0xE9EC`.

The importer already re-signs saves, so the conversion needed no new cryptography.

The first run used the local `bin/burnout3`, which its log identified as 0.1.2-beta, game `f2f5e9bf4d-dirty`, toolkit `e9913cddb8`. It reached the main menu and Single Event selection in 31 seconds. The game's unlock counts went from 3 cars, 1 race event and 1 crash junction before loading to 67 cars, 73 race events and 100 crash junctions after. No unlock-all switch was on.

The second run used the installed `/Applications/Takedawn.app` (0.2.1, game `4e79aeca54`, toolkit `e01f04c4e0`). It loaded the converted save, opened Driver Details and reached car selection in 42 seconds. Driver Details showed 100% completion, 173/173 events, 3,051,605 Burnout points, 3,521 takedowns and $142,084,904 crash damage. The unlock counts again read 67 cars, 73 race events and 100 crash junctions. The build IDs identify the tested installation and may differ from the public release commits.

Both runs used `/tmp/bo3-save-assessment/run/saves` and an explicit settings override. The original local save's SHA-256 was the same afterwards.

## Remaining work (as of 2026-10-22)

- Check partly completed careers, individual medals, trophies, records, vehicle IDs and unlocks. One all-gold profile loading does not prove every field.
- Finish an event, save, restart, and check that the imported progress is still right.
- Let the player pick a profile when several slots are in use.
- Build or reuse the Xbox metadata on a fresh Takedawn install; the experiment used an existing Xbox container.
- Check input bounds, versions, checksums and empty slots, and write through the existing backup and import flow.
- Test on the Steam Deck; the experiment ran on macOS.
- Read `.ps2` memory card images. The [ICO importer documentation](https://github.com/nathanialf/ico-pc/blob/main/docs/PORTABLE_MODE.md#bring-a-ps2-save-over) describes extraction, which is a separate step from the conversion. No ICO code was used.
- Leave PAL and other regions out until real samples are checked.

## Effort estimate (as of 2026-10-22)

| Scope | Expected effort |
|---|---|
| Prove one US folder save loads | Done in the experiment |
| Small standalone US converter with validation, slot choice and save/reload testing | About 1 to 2 development days |
| Into the app's importer, fresh installs, docs and a Deck test | About 2 to 4 development days in all |
| All regions, memory card images and broad compatibility | Needed more samples to estimate |

At that point the format was understood. What was left was checking compatibility and finishing the import flow.

## Local evidence

The experiment's files were under `/tmp/bo3-save-assessment/` and are gone since a restart. The conversion script survives in the Codex session log of 2026-10-22.
