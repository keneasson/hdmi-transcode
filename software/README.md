# software

The hdmi-transcode application. C++17, CMake, built **on the board** for now (Debian
bookworm image, see `docs/bringup.md`); cross-compiling comes with the Buildroot image.

## Build and test (on the board)

```sh
# one-time: toolchain + libdrm headers. libdrm-dev comes from bookworm-backports
# because the Radxa image ships a newer libdrm2 than Debian main's -dev package.
sudo apt-get install -y build-essential cmake pkg-config libudev-dev
sudo apt-get install -y -t bookworm-backports libdrm-dev libdrm-tests

cmake -S . -B build && cmake --build build -j4
ctest --test-dir build --output-on-failure     # no display needed
```

From the Mac, push a working copy to the board (it has no rsync):

```sh
tar -czf - software | ssh radxa@<board> 'rm -rf ~/hdmi-transcode/software && mkdir -p ~/hdmi-transcode && tar -xzf - -C ~/hdmi-transcode'
```

## Run

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

## Layout

- `src/drm_output.*`: owns the DRM fd/master, the scanout buffer and the CRTC (R1, R2, N6).
- `src/edid.*`: reads the display's own preferred timing from its EDID.
- `src/mode_select.*`: the mode choice (R2, R2a): request → EDID preferred → fallback
  ladder 1280x800 / 1024x768 / 640x480 @60 → anything progressive → built-in 1280x800.
- `src/hotplug.*`: udev listener for DRM hotplug events (R2b).
- `src/test_pattern.*`: the generated test pattern (R4), pure drawing into a `Surface`.
- `src/pattern_main.cpp`: the `hdmi-transcode-pattern` tool.
- `tests/`: unit tests for the pattern, EDID parsing and mode choice (fixture: the
  SyncMaster 2433's EDID, serial scrubbed). Run anywhere libdrm headers exist.
