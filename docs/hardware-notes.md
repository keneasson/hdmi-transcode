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

## Image facts (from inspecting the file, not the board)

| Item | Value | Date |
|---|---|---|
| Pinned image | `radxa-zero3_debian_bullseye_cli_b6.img.xz`, release `b6` 2024-01-10 | 2026-10-04 |
| Decompressed size / sha512 | 2 549 646 336 bytes; hash pinned in `scripts/fetch-image.sh`, verified | 2026-10-04 |
| Partitions | 1 `config` FAT 16 MB @ 16777216; 2 `boot` FAT 314 MB empty; 3 rootfs ext4 2.2 GB | 2026-10-04 |
| First-boot mechanism | rsetup `before.txt` in `config`; `headless`-only SSH by default | 2026-10-04 |
| Default accounts | `radxa`/`radxa`, `rock`/`rock` (sudo) | 2026-10-04 |

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
