# software

The hdmi-transcode application. C++17, CMake, built **on the board** for now (Debian
bookworm image, see `docs/bringup.md`); cross-compiling comes with the Buildroot image.

## Build and test (on the board)

```sh
# one-time: toolchain + libdrm headers. libdrm-dev comes from bookworm-backports
# because the Radxa image ships a newer libdrm2 than Debian main's -dev package.
sudo apt-get install -y build-essential cmake pkg-config libudev-dev librga-dev libpng-dev
sudo apt-get install -y -t bookworm-backports libdrm-dev libdrm-tests

cmake -S . -B build && cmake --build build -j4
ctest --test-dir build --output-on-failure     # no display needed
```

From the Mac, push a working copy to the board (it has no rsync); the board's `build/`
tree is kept so incremental builds stay fast:

```sh
tar -czf - --exclude build software | ssh radxa@<board> 'mkdir -p ~/hdmi-transcode/software && cd ~/hdmi-transcode/software && rm -rf src tests CMakeLists.txt && tar -xzf - -C ~/hdmi-transcode'
```

## Run

The application (capture → RGA → display):

```sh
sudo build/hdmi-transcode                                # automatic mode, compress
sudo build/hdmi-transcode --mode 1280x800@60 --aspect letterbox
sudo build/hdmi-transcode --snapshot /run/frame.ppm      # 120th video frame, every still
```

It logs the output mode, input changes, and every 10 s: input fps, shown fps and RGA time.

**Configuration page** (R9, R12, R13): `http://<box>/` from a phone or laptop on the same
network: what the projector shows (live / slide / test pattern), picture shape, projector
resolution (Automatic recommended), and the slide editor. Changes apply immediately and
are saved to `/var/lib/hdmi-transcode/settings` (R5); the slide is
`/var/lib/hdmi-transcode/slide.png` plus `slide.json` (the page's description of it, for
editing; `--slide-dir` moves both). The JSON API behind it:
- `GET /api/status`
- `POST /api/settings`: form fields `aspect`, `mode`, `show` (`input|slide|pattern`)
- `GET /api/slide`: the saved description, or `null`
- `POST /api/slide`: form fields `png` (base64 or a `data:` URL, ≤ 4 MB, ≤ 4096 px a
  side) and `spec`; `DELETE /api/slide` removes it

Changes need the header `X-HT-Request: 1`. Only private-network clients are served
(R11a). The page draws the slide itself (canvas, at the projector's resolution) and
uploads the picture; the box only decodes and shows PNGs. New slide part types go in
`PARTS` in `src/web_page.h`.

With no input it shows the slide, or the test pattern labelled NO INPUT if there is no
slide. Only one process may use the capture device at a time (N8): stop the app before
running `v4l2-ctl` streaming.

The test-pattern tool:

The program needs to be DRM master, so nothing else may own the display (the image's
desktop is disabled: `systemctl disable --now gdm`, default target `multi-user`).

```sh
build/hdmi-transcode-pattern --list                 # EDID, offered modes, automatic choice
sudo build/hdmi-transcode-pattern --mode 1280x800@60  # until Ctrl-C / SIGTERM
sudo build/hdmi-transcode-pattern --seconds 60        # automatic choice (R2/R2a), 60 s
```

On exit the previous screen contents are restored. A display unplugged and plugged back
in, or swapped, is re-detected and re-driven without a restart (R2b).

Test hot-plug without touching the cable (the kernel sends the same HOTPLUG uevent):

```sh
echo off    | sudo tee /sys/class/drm/card0-HDMI-A-1/status   # "unplug"
echo detect | sudo tee /sys/class/drm/card0-HDMI-A-1/status   # back to the real state
```

## Install on the board (boot straight into the app)

```sh
cmake --build build && sudo packaging/install-board.sh && sudo reboot
```

`install-board.sh` installs the binary and `hdmi-transcode.service`, disables the desktop
and the tty1 login, and drops `splash`/`console=tty1` from the kernel command line (N4,
N7). Settings until the web page exists: `/etc/default/hdmi-transcode`, then
`sudo systemctl restart hdmi-transcode`. Logs: `sudo journalctl -u hdmi-transcode -f`.
To get the board back to a normal console: `sudo systemctl disable --now
hdmi-transcode && sudo systemctl unmask getty@tty1`, and restore
`/etc/kernel/cmdline.pre-hdmi-transcode` + `sudo u-boot-update`.

## Layout

- `src/drm_output.*`: owns the DRM fd/master, the scanout buffer and the CRTC (R1, R2, N6).
- `src/edid.*`: reads the display's own preferred timing from its EDID.
- `src/mode_select.*`: the mode choice (R2, R2a): request → EDID preferred → fallback
  ladder 1280x800 / 1024x768 / 640x480 @60 → anything progressive → built-in 1280x800.
- `src/hotplug.*`: udev listener for DRM hotplug events (R2b).
- `src/aspect.*`: where the picture goes for each aspect mode (R3), pure geometry.
- `src/capture.*`, `src/capture_edid.h`: V4L2 capture via TC358743 + ISP, our upstream
  EDID (R6, R7, N8).
- `src/rga_scaler.*`: RGA scale + YUV→RGB into the scanout buffer, black bars (N2).
- `src/settings.*`: persisted settings (R5); `src/file_util.*`: atomic file save.
- `src/slide.*`: the slide store, PNG decoding (libpng), fitting a picture to the screen
  (R13).
- `src/http_server.*`, `src/web_page.h`: the configuration page, slide editor and API
  (R9, R11a, R12, R13).
- `src/app_main.cpp`: the `hdmi-transcode` event loop.
- `packaging/`: systemd unit, defaults file, board install script.
- `src/test_pattern.*`: the generated test pattern (R4), pure drawing into a `Surface`.
- `src/pattern_main.cpp`: the `hdmi-transcode-pattern` tool.
- `tests/`: unit tests for the pattern, EDID parsing and mode choice (fixture: the
  SyncMaster 2433's EDID, serial scrubbed). Run anywhere libdrm headers exist.
