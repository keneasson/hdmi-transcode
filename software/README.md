# software

The hdmi-transcode application. C++17, CMake, built **on the board** for now (Debian
bookworm image, see `docs/bringup.md`); cross-compiling comes with the Buildroot image.

## Build and test (on the board)

```sh
# one-time: toolchain + libdrm headers. libdrm-dev comes from bookworm-backports
# because the Radxa image ships a newer libdrm2 than Debian main's -dev package.
sudo apt-get install -y build-essential cmake pkg-config
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
build/hdmi-transcode-pattern --list                 # modes the display offers
sudo build/hdmi-transcode-pattern --mode 1280x800@60  # until Ctrl-C / SIGTERM
sudo build/hdmi-transcode-pattern --seconds 60        # preferred mode, 60 s
```

On exit the previous screen contents are restored.

## Layout

- `src/drm_output.*`: owns the DRM fd/master, the scanout buffer and the CRTC (R1, R2, N6).
- `src/test_pattern.*`: the generated test pattern (R4), pure drawing into a `Surface`.
- `src/pattern_main.cpp`: the `hdmi-transcode-pattern` tool.
- `tests/`: unit tests for the pattern and mode parsing (run anywhere libdrm headers exist).
