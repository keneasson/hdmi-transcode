# Hardware notes

Findings from the **real board**, filled in as `bringup.md` is run. Nothing here is
a spec-sheet claim; if a line is empty, we don't know yet. Date each entry.

## Board and image

| Item | Value | Date |
|---|---|---|
| `/proc/device-tree/model` | | |
| Image file name | | |
| `uname -r` | | |
| Default user changed | no | |
| Boot medium (SD / eMMC) | | |

## Zero 3W ports (from Radxa docs, https://docs.radxa.com/en/zero/zero3, 2026-10-05)

| Item | Value | Date |
|---|---|---|
| USB-C ports | 2: "USB 2.0 Type-C OTG" = **power input** ("USB-C 1", 5 V/2 A) and "USB 3.0 Type-C HOST" | 2026-10-05 |
| Which is which physically | **Confirmed from the underside silkscreen (photos 2026-10-05):** the USB-C at the **corner next to the microSD slot** is `USB_OTG` / `5V IN` = power. The **middle** USB-C is `USB3.0` = host. Micro HDMI at the far end of the same edge. | 2026-10-05 |
| Other markings | `Maskrom` push button beside the HDMI port (not in the docs page); `TF` = microSD slot, underside, power-port end; board revision silkscreen `ZERO 3W V1.12` | 2026-10-05 |
| Camera connector | "1x 4-lane MIPI CSI (22-pin, 0.5 mm pitch)" → use the C790's 22-pin 0.5 mm cable; 4 lanes allows 1080p60 (confirm in device tree, bringup §5e) | 2026-10-05 |
| Display out | micro HDMI, 1080p60 | 2026-10-05 |
| Radio | Wi-Fi 6 + BT 5.4 (3W) | 2026-10-05 |

## Boards on hand

| Board | RAM / storage | Notes | Date |
|---|---|---|---|
| A | 4 GB / 32 GB eMMC | eMMC arrived **with a system already installed** (3 partitions, chip `07S000`, 29.3 GiB); intended production-like board, gets the C790 | 2026-10-11 |
| B | 2 GB / no eMMC | SD-only; bench board for bring-up and output-side work | 2026-10-11 |

## Image facts (from inspecting the file, not the board)

| Item | Value | Date |
|---|---|---|
| Pinned image | `radxa-zero3_bookworm_kde_b1`, release `rsdk-b1` 2025-07-15 (pins in `scripts/image.env`) | 2026-10-11 |
| Size / sha512 | 1 454 086 428 bytes compressed (8 074 662 912 decompressed); sha512 of the `.xz` pinned, verified | 2026-10-11 |
| Kernel | `6.1.84-10-rk2410-nocsf` (from the image's extlinux.conf / manifest) | 2026-10-11 |
| Partitions | 1 `config` FAT 16 MB @ 16777216; 2 `boot` FAT 300 MB empty; 3 rootfs ext4 7.2 GB @ 348127232 | 2026-10-11 |
| First-boot mechanism | rsetup 0.4.16 `before.txt` in `config`; `headless`-only SSH by default (same lines as b6) | 2026-10-11 |
| Default accounts | `radxa`/`radxa`, `rock`/`rock` (sudo) | 2026-10-11 |
| Previous pin | `radxa-zero3_debian_bullseye_cli_b6` (2024-01-10, kernel 5.10): **does not boot to userspace on our boards**, see gotchas | 2026-10-11 |

## Target projector: NEC NP3151W (from spec sheet, not yet measured)

| Item | Value | Date |
|---|---|---|
| Native | 1280×800, 16:10, 3LCD, 4000 lm ANSI | 2026-10-05 |
| Digital input | DVI-D | 2026-10-05 |
| Listed video formats | 720p, 1080i, 576i/p, 480i/p — **no 1080p**; max input 1600×1200 | 2026-10-05 |
| EDID dump taken? | | |
| Preferred mode in EDID | | |
| Accepts 1280×800@60 over DVI? | | |
| Accepts 1920×1080@60 over DVI? (expect no) | | |
| Physical access | **No.** Bench testing uses the SyncMaster 2433 over the same HDMI→DVI cable; the projector's rows above get filled at the venue. | 2026-10-05 |

## HDMI output (bringup §5)

| Item | Value | Date |
|---|---|---|
| Connector name in `/sys/class/drm` | | |
| SyncMaster 2433 EDID readable over DVI? | | |
| Preferred mode reported | | |
| 1024×768@60 in mode list? | | |
| `modetest` colour bars at 1024×768@60 worked? | | |
| `/dev/rga` present | | |
| Kernel has TC358743 driver (built-in / module / absent) | | |

## CSI / camera connector (bringup §5d–5e)

| Item | Value | Date |
|---|---|---|
| Camera connector pin count | 22-pin 0.5 mm, confirmed by eye: `CSI` FPC on the short edge at the HDMI end, contacts toward the board | 2026-10-05 |
| Which C790 cable fits (15-pin 1.0 mm / 22-pin 0.5 mm) | 22-pin 0.5 mm | 2026-10-05 |
| `data-lanes` in device tree | | |
| Overlays listed that mention csi/camera/tc358 | | |

## WiFi (bringup §6)

| Item | Value | Date |
|---|---|---|
| `iw list` supports AP mode? | | |
| AP + client at the same time? | | |
| Hotspot from a phone worked? | | |

## Capture (bringup §8, after the C790 arrives)

| Item | Value | Date |
|---|---|---|
| TC358743 seen on I2C bus / address | | |
| `/dev/video*` and `/dev/v4l-subdev*` created | | |
| `--query-dv-timings` result (this answers the ATEM frame rate) | | |
| GStreamer smoke test showed a picture? | | |

## Gotchas discovered

Free-form, newest first. Anything that cost more than ten minutes goes here.

- **2026-10-11, b6 image hangs ~10 s into boot on both boards.** Board A (eMMC) and
  board B (no eMMC) both stopped right after the journal flush, with the LED going
  out at about the same moment, the HDMI console frozen on a static cursor, and
  `before.txt` never consumed (rsetup.service never ran). Read with
  `scripts/card-diag.sh`. Cause, **likely but unconfirmed on the board**: Radxa
  changed the 3W's WiFi module after launch, and images from before the change don't
  support it ([forum](https://forum.radxa.com/t/radxa-zero-3w-wifi-problem/25430)).
  The log stops where the kernel powers the SDIO WiFi. Switched to `rsdk-b1`.
  Lesson: pin the newest vendor image, not one from memory.
- **2026-10-11, reading ext4 off a power-cut card shows a stale filesystem.**
  `debugfs` reads blocks without replaying the ext4 journal, so changes committed
  only to the journal before the power cut are invisible until Linux next mounts the
  card. That made a board-A boot look like it came from board B. Treat card-diag's
  rootfs view as "as of the last checkpoint". The `config` partition is FAT (no
  journal) and is reliable.
- **2026-10-11, board A's eMMC has its own system.** Its partitions may carry the
  same UUIDs as an SD image of the same build, and `/config` is mounted by UUID, so an
  SD boot on board A could end up reading the eMMC's `/config`. Not the cause of the
  hang above (board B has no eMMC and hung the same way), but plan to flash board A's
  eMMC directly (Maskrom + `rkdeveloptool`, bringup §7) rather than boot it from SD.

- **2026-10-05, first boot attempt #1: stalled with no WiFi.** Screen froze after
  `GROWROOT: CHANGED: partition=3 ... size=124055519` (the resize itself worked:
  partition 3 is 63.5 GB afterwards). Board never appeared on the LAN. Pulled the card
  and read the `config` partition with mtools: `before.txt` still present (rsetup
  never finished), and its line 62 was `connect_wi-fi Easson` with **no password**:
  `prepare-firstboot.sh` had accepted an empty password prompt. rsetup's
  `connect_wi-fi` retries `nmcli device wifi connect` 10× (1 s apart) and cannot
  succeed without a password. Fixed the script to refuse empty passwords, require a
  confirmation, and verify the written line's field count. Lesson: a frozen console
  after growroot does not mean a kernel hang; the first-boot service runs with its
  output in the journal, not on the console.
