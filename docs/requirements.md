# Requirements

Living document. Decisions made in conversation get written here; anything not here is
still open. Last updated 2026-10-05. Project: https://github.com/keneasson/hdmi-transcode

## Problem

Small venues (churches, halls, classrooms) often have an older 4:3 projector and a modern
16:9 streaming/recording mixer (e.g. ATEM Mini). A presenter's laptop must feed both. Cheap
HDMI splitters re-stream the signal and force every device in the chain to agree on one
EDID/handshake, so the projector and the mixer fight each other and the result is a
flickering or black picture and a volunteer "scratching their head".

This project is a small box that sits on the *projector* leg of the splitter, accepts
whatever the mixer locked the chain to (1080p), and re-outputs it in a form the attached
display accepts, with the aspect handling selectable by the user.

## Design principle

**Accept whatever arrives; output something stable.** The box is the least demanding
device on the input side and the most predictable device on the output side. It never
causes a handshake failure upstream and never causes a display to blank downstream.

### EDID in this chain

EDID is not a negotiation between sinks. The presenter's laptop reads only the
splitter's EDID; the splitter's setting decides whether that is its own fixed profile
(Muxlab "setting 1" = fixed 1080p) or a copy of one of its outputs' EDIDs. The ATEM's
pickiness and this box's EDID reach the laptop only through that setting. Consequences:

- If the ATEM inputs must dictate the format, use the splitter setting that copies EDID
  from the ATEM's output; this box then simply receives the result.
- This box cannot "step back" from the handshake, but it can be so permissive that it
  never constrains it (R7). That is the practical equivalent.
- HDCP: the TC358743 on hobby boards has no HDCP keys, so protected content cannot be
  captured. In an ATEM chain this is already the case (the ATEM rejects HDCP; most
  splitters strip it), so it is not a new limitation, but the box cannot fix it either.

## Functional requirements

### Output (buildable now, no capture hardware needed)

- R1. Drive a display over the Zero 3W HDMI port (HDMI → DVI-D adapter for DVI displays).
- R2. Read the attached display's EDID and offer only modes it supports; pick a sensible
  default (its preferred/native mode).
- R2b. **Display hot-plug.** A display connected (or power-cycled) after the box has
  booted must be detected and driven without a reboot: watch DRM hot-plug events and
  mode-set when a connector becomes active. Observed 2026-10-05: the stock kernel
  console only sets up the display present at boot, so this cannot be left to the
  kernel.
- R2a. **Cope with bad or missing display EDID** (common on older DVI projectors).
  Fallback ladder: display preferred mode → 1280×800@60 (primary target) → 1024×768@60
  → 640×480@60 (mandatory for every DVI/HDMI sink). Always overridable from the config UI, and the chosen mode is
  remembered (R5) so the fallback search happens once, not every boot.
- R3. Aspect-handling modes for a 16:9 source on a non-16:9 display (for the primary
  16:10 target: compress = 11 % vertical stretch, letterbox = 40 px bars top/bottom,
  crop = 5 % off each side):
  - **Compress** (default, confirmed by Ken 2026-10-05): anamorphic squeeze to fill
    the screen, nothing cut off.
  - **Letterbox**: preserve aspect, black bars.
  - **Crop**: preserve aspect, fill screen, trim edges.
  - **Native**: plain scale-to-fit with no aspect correction.
  (16:9 → 16:9 displays are a pass-through scale.)
- R4. A built-in **test pattern** generated at the exact output resolution, shown when
  there is no input or on request. It must make the current mode self-evident: circles
  (distortion is visible under Compress), 4:3 and 16:9 safe-area frames, grey ramp and
  colour bars for the display, and a text overlay of the current output mode, resolution
  and refresh rate. The stock "please stand by" card in the project notes is a reference
  for content only, not an asset we ship.
- R5. Settings persist across power cycles. The box must come up unattended into the last
  working configuration; a venue cannot be expected to reconfigure it each Sunday.

### Input (after the HDMI-to-CSI board arrives)

- R6. Capture **whatever arrives** that the TC358743 can handle (720p and 1080p at all
  common frame rates, up to 1080p60 if the CSI lane count allows). Detect timing,
  frame rate and colourspace at runtime, never hardcode them, and re-detect on change.
- R7. Present a **maximally permissive EDID** on the capture side: advertise every mode
  the bridge can capture, 1080p60 preferred. The box must never be the device that
  constrains the upstream handshake (see "EDID in this chain" above).
- R8. On signal loss, show the test pattern (R4) rather than a black screen, so the
  operator can tell "box is fine, source is gone" from "box is dead".
- R8a. **Input changes never change the output mode.** A source swap (1080p laptop →
  720p laptop), signal loss, or re-handshake upstream is absorbed by the scaler; the
  display is never re-modeset and never blanks/resyncs because of something upstream.

### Configuration

- R9. Configurable from a phone/laptop over **WiFi via a web page**. First
  implementation: the box **hosts its own hotspot** (SSID `hdmi-transcode`, a captive
  landing page or `http://hdmi-transcode.local`). Joining the venue WiFi with an mDNS
  name comes second and the hotspot remains the fallback when no venue network is
  configured or reachable. The hotspot password is set at first boot / per unit, never
  committed.
- R10. Configurable over **USB** (the board's USB port) as a second path, exact mechanism
  TBD (serial console, or USB gadget Ethernet presenting the same web page).
- R11. **Stretch goal:** a Bluetooth phone app using the same settings API as R9.
- R11a. **Network exposure.** The config web UI and SSH listen only on the box's own
  hotspot subnet and on RFC 1918 (private) LAN addresses; nothing is ever reachable
  from the internet, and the box never asks a router for port forwarding/UPnP. Default
  credentials are forced to change on first use. Being on a private 10.x/192.168.x
  subnet is a baseline, not the security: anyone on the venue LAN can reach the box,
  so the UI itself must be safe to expose to the LAN.
- R12. Configuration surface: output mode/resolution, aspect mode (R3), show/hide test
  pattern, network settings. Nothing that requires understanding video signalling.

## Non-functional

- N1. Latency low enough for live use on a projector; a few frames is acceptable. A
  precise budget is TBD once measured.
- N2. No CPU pixel processing: capture → hardware scaler → display, DMA-BUF between
  stages.
- N3. Audio is out of scope (DVI has none; the mixer handles audio).
- N4. Minimal OS, no display server; the app owns the display via DRM/KMS. Boot straight
  into the app.
- N5. Licensed BSD-3-Clause (see `LICENSE`).

## Open questions

- ~~Projector model~~ **Decided 2026-10-05:** NEC NP3151W, 1280×800 (16:10) native,
  DVI-D. Spec source: https://www.projectorcentral.com/nec-np3151w.htm (lists 720p,
  1080i, max input 1600×1200; **1080p not listed**). To verify on the unit: its EDID,
  whether its DVI accepts 1280×800@60 as preferred, and whether it tolerates
  1920×1080@60 at all. 1024×768@60 stays as the generic 4:3 secondary target. The
  SyncMaster 2433 is the bench display.
- ATEM Mini frame rate in use (59.94 vs 50) — read from the bridge once connected.
- Zero 3W CSI lane count (decides 1080p60 vs 1080p30 capture).
- Production Buildroot image details (decided: Radxa headless image for bring-up,
  Buildroot on the Rockchip BSP kernel for production; see CLAUDE.md).
- USB configuration mechanism (R10).
