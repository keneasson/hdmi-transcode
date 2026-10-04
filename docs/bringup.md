# Bring-up: Radxa Zero 3W

Goal of day one: boot a headless image, prove the HDMI **output** path on the SyncMaster
with a hardware-drawn test pattern, and answer the open hardware questions in
`hardware-notes.md`. The capture side (C790 / TC358743) has its own section at the end
for when the board arrives.

Nothing here flashes, reboots or changes the device tree without you doing it on
purpose. Steps marked **[hardware-touching]** are the ones to re-read before running.

## 0. What you need

- Radxa Zero 3W (4 GB / 32 GB eMMC), its USB-C power supply (5 V, 2 A or better; a
  weak supply causes random resets that look like software bugs).
- microSD card, 8 GB+, and a reader.
- micro-HDMI → HDMI cable or adapter, then HDMI → DVI-D adapter to the SyncMaster.
- A way to talk to the board: a USB keyboard on the OTG port **or** a USB-serial
  adapter on the UART header **or** WiFi (the headless image lets you set WiFi up
  from a first-boot wizard or by dropping a config on the boot partition; see Radxa
  docs). Serial is the most reliable for first boot because it shows the kernel log.

## Mac-side tools

Everything below runs on the Mac. Installed via Homebrew; each one is here because a
step needs it:

| Tool | Why | Install |
|---|---|---|
| `gh` | download pinned release assets, PRs | `brew install gh` |
| `xz` | decompress the image | ships with macOS |
| `mtools` | edit the image's FAT `config` partition; macOS cannot mount it | `brew install mtools` |

## 1. Image choice

Use Radxa's **official headless/CLI image** for the Zero 3W (Debian-based, Rockchip
BSP kernel). Not the desktop variant: a display server would own the HDMI output we
need to drive ourselves.

**Pinned build:** `radxa-zero3_debian_bullseye_cli_b6.img.xz` from release `b6`
(2024-01-10) of https://github.com/radxa-build/radxa-zero3/releases. It is the newest
release that ships a CLI variant; the later `rsdk-*` releases are KDE desktop builds
(fallback if `b6` lacks the `tc358743` driver: `rsdk-b1`, Bookworm, and disable its
display manager). The pin and the published sha512 live in `scripts/fetch-image.sh`;
change them together.

```sh
scripts/fetch-image.sh            # downloads to ~/Downloads/radxa and verifies sha512
```

Docs for the board: https://docs.radxa.com/en/zero/zero3. Record the exact image and
kernel version in `hardware-notes.md` once booted.

## 2. Write the image to microSD (lowest-risk first boot)

Boot from microSD first. It leaves the eMMC untouched, so a bad image or a wrong
overlay costs a re-flash of a card, not a board. Moving to eMMC is a later step.

On the Mac, with the card in a reader, **in a Terminal** (it prompts for `sudo` and
for confirmation, so not from inside Claude Code):

```sh
diskutil list external physical   # find the card, e.g. /dev/disk5 — CHECK THIS TWICE
scripts/flash-sd.sh /dev/diskN
```

**[hardware-touching]** The script refuses internal or virtual disks and partition
identifiers, shows what it is about to erase, and requires the identifier typed back
before it runs `dd`. Balena Etcher is a fine alternative if you'd rather click.

## 2a. Prepare first boot: WiFi + SSH with no keyboard

Facts found by inspecting the b6 image (2026-10-04), not from docs:

- GPT layout: partition 1 `config` (16 MB FAT, byte offset 16777216), partition 2
  `boot` (314 MB FAT, **empty**; U-Boot loads the kernel from the rootfs), partition 3
  rootfs (ext4, 2.2 GB, grows to fill the card on first boot via `resize_root`).
- `config` holds `before.txt` and `config.txt`, run by Radxa's `rsetup` first-boot
  service and then deleted. Commands available include `add_user`, `connect_wi-fi
  <ssid> [password]`, `enable_service`/`disable_service`, `update_locale`.
  Reference: https://github.com/radxa-pkg/rsetup/blob/main/config/before.txt
- Default accounts created by `before.txt`: `radxa`/`radxa` and `rock`/`rock`, both
  in `sudo`, `video`, `render`, `i2c`, `gpio`. Change the passwords on first login.
- **Trap:** the default is `disable_service ssh` then `if headless enable_service
  ssh`, where *headless* means "no display connector attached". With the SyncMaster
  plugged in, SSH stays **off** unless we enable it explicitly.
- macOS cannot mount the `config` volume (`mount_msdos: Invalid argument`), so it is
  edited with `mtools` at the raw offset before flashing.

- **Constraint (from reading rsetup's `rconfig.sh`):** each line of `before.txt` is
  split on whitespace with no quote handling and executed as-is. The SSID and the
  password therefore cannot contain spaces or tabs. rsetup also echoes the command,
  password included, to the first-boot journal on the board.

`scripts/prepare-firstboot.sh` does this on a copy of the image: prompts for the home
WiFi SSID/password (stored only inside that copy), refuses values with whitespace,
sets `connect_wi-fi`, and replaces the conditional with an unconditional
`enable_service ssh`. A failed run removes the partial copy.

```sh
scripts/prepare-firstboot.sh        # -> ~/Downloads/radxa/..._b6-firstboot.img
scripts/flash-sd.sh /dev/diskN      # picks the -firstboot.img automatically
```

The `-firstboot.img` copy contains the WiFi password in plain text. It lives in
`~/Downloads/radxa`, is git-ignored, and can be deleted after the card is written.

## 3. First boot checklist

Insert the card, connect the SyncMaster, power on. First boot takes a few minutes
(rsetup runs `before.txt`, resizes the root filesystem, then reboots). Expect the
kernel console on the display and/or serial.

Finding the board with no keyboard (after §2a): it joins the home WiFi. Try, in order:

```sh
ssh radxa@radxa-zero3.local            # if mDNS resolves (hostname unverified; check)
arp -a | grep -i -v incomplete         # look for a new address on the LAN
# or the router's client list
ssh radxa@<ip>                         # password: radxa — change it immediately
```

Run these and paste the output into `hardware-notes.md`:

```sh
cat /proc/device-tree/model; echo
uname -a
cat /etc/os-release | head -3
cat /proc/cmdline
```

Networking (needed for the next steps):

```sh
ip link
nmcli dev status                   # Radxa images use NetworkManager
# join your home WiFi for bring-up (venue hotspot mode comes later):
nmcli dev wifi connect "<ssid>" password "<password>"
```

Install the tools used below:

```sh
sudo apt update
sudo apt install -y libdrm-tests edid-decode v4l-utils i2c-tools device-tree-compiler
```

## 4. Prove the HDMI output path (the day-one win)

### 4a. What does the display controller see?

```sh
ls /sys/class/drm/
cat /sys/class/drm/card*-HDMI-A-1/status            # expect "connected"
edid-decode < /sys/class/drm/card*-HDMI-A-1/edid    # the SyncMaster's EDID, via DVI
modetest -M rockchip -c                             # connectors + the mode list
```

Save the raw EDID for the repo (it's your own monitor, so fine to commit):

```sh
cp /sys/class/drm/card*-HDMI-A-1/edid ~/syncmaster-2433.edid.bin
```

Open questions this answers: does the DVI link report a usable EDID, is 1024×768@60
in its mode list, what is its preferred mode.

### 4b. Draw a test pattern with no software of ours

`modetest` can set a mode and fill the screen with SMPTE bars straight through
DRM/KMS. Find the connector id and a crtc id in the `-c` / `-p` output, then:

```sh
sudo modetest -M rockchip -s <connector-id>@<crtc-id>:1024x768-60
# Ctrl-C to release. Try the display's preferred mode too.
```

If the SyncMaster shows colour bars at 1024×768, the entire output side of this
project is proven at the hardware level: R1, R2 and the fallback ladder in R2a are
now about software, not hardware. If the console tty grabs the display back when
modetest exits, that's expected.

Also confirm the hardware scaler exists:

```sh
ls -l /dev/rga
dmesg | grep -i rga
```

### 4c. Kernel features we depend on

```sh
# whichever of these the image provides:
zcat /proc/config.gz 2>/dev/null | grep -E "TC358743|ROCKCHIP_RGA|VIDEO_ROCKCHIP|DRM_ROCKCHIP|DW_HDMI"
grep -E "TC358743|ROCKCHIP_RGA|VIDEO_ROCKCHIP|DRM_ROCKCHIP|DW_HDMI" /boot/config-$(uname -r) 2>/dev/null
# is the tc358743 driver present at all?
find /lib/modules/$(uname -r) -name 'tc358743*'
```

If `TC358743` is missing from the kernel entirely, that decides a lot: either a
different Radxa image/kernel, or we build the module. Note it prominently.

### 4d. Device-tree overlays available

```sh
sudo rsetup                        # Radxa's config tool; look under Overlays
ls /boot/dtbo/ 2>/dev/null; ls /usr/lib/linux-image-*/rockchip/overlay/ 2>/dev/null
ls /boot/dtbo/ /usr/lib/linux-image-*/rockchip/overlay/ 2>/dev/null | grep -i -E "csi|camera|tc358|hdmi"
```

We're looking for an overlay that enables the CSI receiver and, ideally, one that
names the TC358743. Don't enable anything yet; just list what exists.

### 4e. How many CSI lanes does this board expose?

This decides 1080p60 (4 lanes) vs 1080p30 (2 lanes) capture. Read it from the live
device tree rather than a spec sheet:

```sh
dtc -I fs -O dts /proc/device-tree 2>/dev/null | grep -n -B3 -A12 -E "csi|mipi" | grep -E "csi|mipi|data-lanes|status" 
```

`data-lanes = <0x01 0x02 0x03 0x04>` means 4 lanes are wired; `<0x01 0x02>` means 2.
Also physically count the pins on the camera connector and note which of the C790's
two FPC cables (15-pin 1.0 mm, 22-pin 0.5 mm) fits, and whether the contacts face the
same way at both ends.

## 5. Hotspot mode (needed for R9)

Just prove the radio can do it; the real implementation comes with the app:

```sh
nmcli dev wifi hotspot ifname wlan0 ssid hdmi-transcode password "<temporary>"
# from a phone: join "hdmi-transcode", then
ip addr show wlan0        # note the box's address
nmcli con down Hotspot    # tear it down when done
```

Note whether the chip supports AP mode at all (`iw list | grep -A8 "Supported interface modes"`
should list `AP`) and whether AP and client can coexist (same output, look for
`interface combinations`).

## 6. Move to eMMC (later, optional) **[hardware-touching]**

Once the SD boot is proven, the same image can go onto the 32 GB eMMC so the card
isn't needed. Radxa documents two ways: `rsetup` from the running SD system, or
`rkdeveloptool` over USB with the board in maskrom mode. Follow their current docs
rather than this file; record which you used.

## 7. When the C790 arrives (capture side)

Power off. Connect the C790 to the camera connector with the cable identified in 4e,
connect splitter output 2 to its HDMI input, power on.

```sh
dmesg | grep -i -E "tc358743|rkcif|csi|v4l"
i2cdetect -l                              # list buses
sudo i2cdetect -y <bus>                   # TC358743 answers at 0x0f when powered and wired
v4l2-ctl --list-devices
v4l2-ctl -d /dev/v4l-subdevN --query-dv-timings     # what's actually arriving from the splitter
v4l2-ctl -d /dev/v4l-subdevN --get-edid=file=current.edid
```

If the chip doesn't appear on I2C, it's cable orientation or the overlay (4d), in
that order. Getting `--query-dv-timings` to report 1920x1080pXX is the capture-side
equivalent of the colour bars in 4b: it answers the ATEM frame-rate question and
proves the hardware end to end.

Then, the first full-pipeline smoke test with no code of ours (GStreamer, if the
image ships it):

```sh
gst-launch-1.0 v4l2src device=/dev/videoN ! video/x-raw,format=UYVY,width=1920,height=1080 ! kmssink
```

Expect to iterate on the format caps; the point is a picture on the SyncMaster.
