# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

A small box, built on a Radxa Zero 3W, that sits on the projector leg of a cheap HDMI
splitter in a small venue (church, hall), takes the 1080p the streaming mixer locked the
chain to, and re-outputs it in whatever form the attached projector accepts, with the
aspect handling (compress / letterbox / crop) selectable from a phone over WiFi. The
point is that a volunteer never has to understand EDIDs to make "the projector and the
stream" work at the same time. Full requirements: `docs/requirements.md`.

The project is **hdmi-transcode**: https://github.com/keneasson/hdmi-transcode (public,
BSD-3-Clause). The repo holds the software, requirements and documentation.

### Signal chain

```
ATEM Mini (1080p)
  └─ Muxlabs 100506 HDMI splitter (EDID position 1, locked at 1080p)
       ├─ Out 1: (ATEM side / passthrough)
       └─ Out 2: HDMI-to-CSI adapter board
                   └─ Radxa Zero 3W (4GB RAM / 32GB eMMC) — MIPI CSI in
                         └─ micro-HDMI out → HDMI-to-DVI → DVI display
```

Input timing: the ATEM Mini outputs 1080p at the frame rate set in its own video-standard
setting (59.94 or 50 are the usual candidates); the splitter's EDID position 1 presents a
fixed EDID to the ATEM so the handshake stays locked (Muxlabs support's recommendation to
stop the ATEM "flickering"). Exact frame rate is **not yet known** — read it from the
TC358743 with `v4l2-ctl --query-dv-timings` once the board is up, and have the software
detect input timing at runtime rather than hardcode it.

Target displays:
- Primary: a DVI-input 4:3 projector (the real use case). No specific model yet;
  **1024×768@60 is the canonical 4:3 design target** until one is in hand.
- Test bench: Samsung SyncMaster 2433 (DVI).

Configuration: phone web page over the box's **own WiFi hotspot** first; joining venue
WiFi second; Bluetooth app is a stretch goal.

Aspect handling is user-selectable: compress (default), letterbox, crop, native — see
R3 in `docs/requirements.md`.

## Project status (as of 2026-10-04)

- Zero 3W is on hand but not yet connected to the dev machine.
- HDMI-to-CSI adapter board is on order: GODIYMODULES "C790" HDMI to CSI-2 module
  (Amazon ASIN B0DJQFJB1C), a Toshiba **TC358743XBG** bridge design (same family as the
  Geekworm C790 / Auvidea B101 / BliKVM boards). Ships with 15-pin 1.0mm and 22-pin
  0.5mm FPC cables plus an I2S audio cable. Advertises 1080p60 and audio over I2S.
  - TC358743 does 1080p60 only over **4 CSI-2 lanes**; on 2 lanes it is limited to
    1080p30. **Open question:** how many lanes the Zero 3W camera connector exposes and
    which of the two FPC cables fits it. Verify on the board, don't assume.
  - Driver: Linux mainline `tc358743` V4L2 subdev driver exists; whether it works with
    the RK3566 `rkcif` capture path depends on the kernel (Radxa BSP vs. mainline).
    Treat this as the main software risk and settle it before committing to an OS image.
  - I2S audio and "backpower mitigation" are out of scope for now.
- No code yet. Everything below under "Tentative" is a plan, not a decision.

### Build order

1. **Output side first** — it needs no capture hardware. Drive the HDMI port via
   DRM/KMS, read the display EDID, render the generated test pattern (req. R4) in each
   aspect mode, persist settings. This is testable on the SyncMaster the day the board
   is plugged in.
2. **Settings + web UI over WiFi** (R9), so the aspect/resolution choice can be made
   from a phone against the test pattern.
3. **Input side** once the C790 arrives: TC358743 bring-up, capture → RGA → the
   already-working output path.
4. USB config path (R10), then the Bluetooth app (R11) as a stretch goal.

## Repo layout (planned)

```
docs/          requirements.md, bringup.md (flash + first-boot checklist),
               hardware-notes.md (findings from the real board; fill in, don't guess)
software/      the application (own build system lives here)
hardware/      wiring, device-tree overlays, board config, EDID dumps
```

Keep hardware bring-up findings (working kernel version, overlay names, v4l2 formats
observed, etc.) in `docs/` as they are discovered — they are hard-won and not derivable
from code.

## Design principle

**Accept whatever arrives; output something stable.** Least demanding device on the
input side (permissive EDID, runtime timing detection), most predictable on the output
side (one fixed display mode, never re-modeset because of an upstream change; test
pattern on signal loss; fallback ladder for displays with bad EDID). If a change makes
the box pickier upstream or less stable downstream, it's wrong. Details and the EDID
explanation: `docs/requirements.md`.

## Tentative technical direction

- **Don't move pixels in software.** On the RK3566 the pipeline should be
  V4L2 capture (CSI bridge) → Rockchip RGA (hardware scale/crop/letterbox) →
  DRM/KMS output on the HDMI port, using DMA-BUF handoff between stages so frames are
  never copied through the CPU. Latency and CPU load are the metrics that matter.
- **Language: C++ (C++17 or later), CMake.** Chosen because V4L2, DRM/KMS and
  `librga` are C APIs and the hot path is driving hardware, not computation. Revisit
  only if a concrete measurement shows a problem.
- **Prototype with GStreamer first** (`v4l2src` → RGA/`kmssink`) to prove the hardware
  path end-to-end before writing custom code. A working gst pipeline is also the
  reference to benchmark the custom app against.
- DVI-D carries HDMI video with no audio; audio is out of scope unless requirements say
  otherwise.
- **OS: as skinny as possible, no display server.** Our process owns the HDMI output
  through DRM/KMS directly; never install X/Wayland on the box. Staging:
  1. Bring-up on Radxa's official **headless/server** image so the Rockchip BSP kernel,
     `rkcif`, RGA, `tc358743` and the overlays are known-good while the hardware is
     being proven. Record every kernel/overlay/firmware finding in `docs/`.
  2. Production image built with **Buildroot** on that same Rockchip kernel, containing
     only our app, networking and the config web server. Alpine on the Radxa kernel is
     the fallback if Buildroot fights us.
  Don't start on Buildroot before step 1 works: two unknowns at once is undebuggable.

## Build / run / test

Not yet defined. When the first code lands, record here:
- the cross-compile vs. on-device build decision and the exact commands,
- how to deploy to and run on the Zero 3W,
- how to run a single test, and which tests need the board attached.

## Working conventions

- Hardware is involved: **never flash, reboot, re-partition, or change the device tree
  on the board without asking first.** Reading state (`v4l2-ctl --list-*`, `dmesg`,
  `modetest`) is fine.
- Commit on a branch and open a PR to `main`; don't commit directly to `main`.
- Run `/pr` (project skill in `.claude/skills/pr/`) before opening or updating any PR.
  It carries this project's review checklist: resource ownership, the frame path,
  field robustness, and the hardware-touching rules.
- Standing permission (Ken, 2026-10-04): run `/pr`, fix its findings, push the branch
  and open the PR without asking each time. Report the link and the verdict.
  **Merging is not covered**: ask before `gh pr merge`.
- When a requirement is decided in conversation, write it into `docs/requirements.md`
  rather than leaving it only in chat history.
- Record unverified hardware assumptions as such (as this file does) until confirmed on
  the actual board.
