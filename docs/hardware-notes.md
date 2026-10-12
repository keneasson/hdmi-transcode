# Hardware notes

Findings from the **real board**, filled in as `bringup.md` is run. Nothing here is
a spec-sheet claim; if a line is empty, we don't know yet. Date each entry.

## Board and image

| Item | Value | Date |
|---|---|---|
| `/proc/device-tree/model` | `Radxa ZERO 3`; compatible `radxa,zero3w-aic8800ds2 radxa,zero3 rockchip,rk3566` (both boards) | 2026-10-11 |
| Image file name | `radxa-zero3_bookworm_kde_b1` (+ `prepare-firstboot.sh`) | 2026-10-11 |
| `uname -r` | `6.1.84-10-rk2410-nocsf` | 2026-10-11 |
| Default user changed | no (`radxa`/`radxa`; SSH key installed for `radxa`) | 2026-10-11 |
| Boot medium (SD / eMMC) | SD (`mmcblk1`). Board A boots the SD card fine even with its factory eMMC system present (different UUIDs, no clash) | 2026-10-11 |
| WiFi chip | AIC8800DS2 (`aic8800_fdrv`), joined `Easson` via `before.txt` on first boot | 2026-10-11 |
| Status LED | heartbeat (double flash, pause) when running; **black screen + LED off does not mean hung** on this image | 2026-10-11 |

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

Connector is on the board's **underside** (same side as the eMMC). Cable seated with
pin-1 marks (triangle on both PCBs, "1" on both cable ends) aligned; C790 green LED
lights from the Zero's supply through the cable (2026-10-11).


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
| Kernel driver | Rockchip `tc35874x` built in (`CONFIG_VIDEO_TC35874X=y`); upstream `CONFIG_VIDEO_TC358743` not set. Also built in: `rkcif`, `rkisp`, `csi2-dphy`, RGA | 2026-10-11 |
| Overlay | `radxa-zero3-tc358743` (in `/boot/dtbo`, shipped disabled): I2C2 @ `0x0f`, **4 data lanes**, routed csi2-dphy0 → **rkisp** (not rkcif). Enabled by renaming `.dtbo.disabled` → `.dtbo` + `u-boot-update`; backup `extlinux.conf.pre-tc358743` | 2026-10-11 |
| TC358743 seen on I2C bus / address | yes: `2-000f`, "tc358743 found", `Lanes in use: 4`, CSI out `YCbCr 422 16-bit`. Without the C790 attached the driver still binds and logs `i2c_rd ... failed` (boot is unaffected) | 2026-10-11 |
| `+5V` cable detect | **always "yes" on the C790, even with nothing plugged in** (board back-powers the HDMI 5V). Can't be used for input presence: use TMDS / stable-sync status instead (R5) | 2026-10-11 |
| EDID | chip starts with **none**; sources won't transmit until one is loaded (`v4l2-ctl -d <subdev> --set-edid=type=hdmi` worked; volatile, gone on reboot) | 2026-10-11 |
| `/dev/video*` and `/dev/v4l-subdev*` created | `/dev/media0`; subdevs: rkisp-isp, rkisp-csi, csi2-dphy0, `m00_b_tc35874x 2-000f`; `/dev/video0..` rkisp mainpath/selfpath/rawwr | 2026-10-11 |
| `--query-dv-timings` result (this answers the ATEM frame rate) | Mac laptop as source (not yet the ATEM): 1920x1080p **60.00**, 148.5 MHz, VIC 16, YCbCr 4:4:4 BT.709 limited, 8-bit, no HDCP. ATEM rate still to measure | 2026-10-11 |
| GStreamer smoke test showed a picture? | not run yet. **Raw capture works:** `v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=NV12 --stream-mmap=4 --stream-count=30` (rkisp mainpath) gave 30 × 3 110 400-byte NV12 frames; a frame converted on the Mac showed the source desktop with correct colour. Graph: csi-subdev UYVY8_2X8 1920x1080 → isp → mainpath; formats UYVY/NV16/NV12/NV21 etc. Board A (4 GB) | 2026-10-11 |

## Gotchas discovered

Free-form, newest first. Anything that cost more than ten minutes goes here.

- **2026-10-11, hot-plug can be tested from SSH.** Writing `off` / `on` / `detect` to
  `/sys/class/drm/card0-HDMI-A-1/status` (root) emits the same udev event as a cable
  change (`ACTION=change`, `HOTPLUG=1` on `card0`); `detect` returns to real detection.
  Verified with `hdmi-transcode-pattern`: off → CRTC inactive; detect → re-driven at the
  EDID mode; a repeat event with the same display → no re-mode-set; started with the
  display "off" → waits, then lights up. Still to confirm with a real cable pull.
  Also: `pkill -f <pattern>` over SSH kills the SSH shell itself (its command line
  contains the pattern); kill by saved PID.

- **2026-10-11, first output from our own code; board A system changes.**
  `hdmi-transcode-pattern --mode 1280x800@60` showed the R4 pattern on the SyncMaster
  exactly as designed. Changes made on board A to get there (record for the Buildroot
  image): desktop disabled (`systemctl disable --now gdm`, default target
  `multi-user.target`); installed `build-essential cmake pkg-config` and, from
  bookworm-backports, `libdrm-dev libdrm-tests`, which moved `libdrm2` from the image's
  `2.4.122+git…oibaf` build to Debian's `2.4.123-1~bpo12+1`. The SyncMaster 2433's mode
  list puts **1920x1080 @ 50Hz** first as "preferred" (it lists 1920x1200@60 too):
  don't trust the preferred flag blindly when picking a default (R2/R2a).

- **2026-10-11, first live picture, then a kernel lock-up on capture restart.**
  Working preview (board A, rsdk-b1, overlay `radxa-zero3-tc358743`, GDM stopped):
  `gst-launch-1.0 v4l2src device=/dev/video0 io-mode=dmabuf !
  video/x-raw,format=NV12,width=1920,height=1080 ! kmssink plane-id=72 sync=false`
  showed the source on the SyncMaster at ~5 % CPU, visibly low latency, with some
  flicker (not yet diagnosed). Traps on the way:
  - `kmssink` defaults to plane 96 **Cluster0-win0, which on RK3566 takes only AFBC**:
    negotiates `arm-afbc=1` then fails with `unable to copy AFBC`. Use plane 72
    `Esmart0-win0` (Smart0 = plane 56 carries the console fb). Our code must pick the
    plane explicitly.
  - `CIF_ISP_PIC_SIZE_ERROR (0x1)` logs once per stream start; harmless.
  - **Lock-up:** killing a streaming pipeline and starting another ~1 s later crashed
    the old process's close path: `BUG: spinlock bad magic ... v4l2_fh_del <-
    rkisp_fop_release`, its thread spun, `rcu_sched detected stalls` 55 s later, WiFi
    commands timed out, board unreachable (heartbeat LED kept going). The interrupt
    table printed with the stall (`17: 0 0 0 0 GICv3 67 Level (null)`) is not the cause.
    Rules for the app: exactly one owner of the capture node; stop streaming, free
    buffers and close fully before any re-open; never overlap open/close. Candidate
    mitigation: route the TC358743 through `rkcif` instead of `rkisp` (needs an overlay
    variant; device-tree change, ask first).

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
