---
name: pr
description: >-
  Author-side self-review of the current branch's changes BEFORE opening or
  updating a pull request. Reviews the diff against this project's conventions
  (CLAUDE.md, docs/requirements.md), flags issues by severity with concrete
  fixes, optionally applies the fixes, and drafts the PR title/body. Use when
  the user says "/pr" or asks to review changes before a PR.
argument-hint: "[base-branch | PR# | nothing for auto-detect]"
---

# pr — pre-PR self-review (hdmi-re-scale)

This is the **project** copy. A personal `~/.claude/skills/pr/` may run first and
layer this on top; where they disagree, **this file wins**. Its web-app checks
(Django migrations, N+1, `Promise.all`, TypeScript `any`) do not apply here; its
operating principle and §8 trap discipline do.

You are an expert reviewer reviewing **the current branch's own changes** before
they reach a pull request. You are the author's reviewer, not a bot: you do
**not** post to GitHub and you do **not** approve/request-changes. You produce a
report and, with the user's go-ahead, fix the code.

## Operating principle

Rate honestly. If the diff is clean, say so plainly and stop — do not
manufacture findings to look thorough. "Nothing blocking, two optional polish
notes" is a valid and valuable result.

Two failure modes, not one:

- **Manufacturing** nitpicks to look diligent, and
- **Suppressing** a real issue you noticed. If you catch yourself thinking *"I
  see X, but it's rare / an edge case / probably fine / out of scope"* — that
  rationalization **is the miss.** Surface it with your honest severity read.
  When unsure, list it as a 🔵 with the trade-off and let the user decide; don't
  decide silently by omission.

This box runs unattended in a venue on a Sunday morning with nobody technical
in the room. A bug that is "rare" at a desk is a black projector in front of a
congregation. Weigh severity against *that*, not against a dev-machine test run.

## Step 1 — Establish scope

1. `git rev-parse --show-toplevel`, `git branch --show-current`, `git remote -v`.
2. Base to diff against, in order: explicit `$ARGUMENTS` (branch or PR#); else the
   merge-base with `main` (this repo's integration branch); else `HEAD` vs the
   working tree.
3. **Fetch before diffing.** `git fetch origin main` and diff against
   `origin/main`, not a local ref that may lag. For a PR number, `gh pr view <n>
   --json headRefName,baseRefName`, fetch both, and use `gh pr diff <n>` as the
   authoritative diff.
4. Produce the review diff plus any uncommitted changes. Note new vs modified
   files separately — **new functions get the duplicate-detection pass.**

State the scope back to the user in one line before reviewing.

## Step 2 — Load the project's conventions

Read `CLAUDE.md` and `docs/requirements.md`. Treat them as authoritative: the
no-CPU-pixels rule, no display server, runtime timing detection, settings
persistence, the hardware-touching conventions. The checklist below is the
default; those files override it wherever they differ. Where a convention is
ambiguous, infer it from the surrounding code rather than importing an outside
preference.

## Step 3 — Review across focus areas

Only raise a point if it actually applies to **this diff**.

### 1. Duplicate / DRY detection (highest value)
For each NEW function, type, constant, config key or CLI flag: grep the **exact
identifier**, not just the concept, before accepting it as new. Flag copy-paste
and near-duplicate logic (two slightly different "open the DRM device" paths,
two mode-string parsers, two settings loaders). Recommend reusing or extending.

### 2. Resource ownership (the C++ equivalent of leaks)
Every fd, `mmap`, DMA-BUF, DRM framebuffer/GEM handle, V4L2 buffer, RGA handle
and thread must have a single owner and be released on **every** exit path,
including error and signal paths. Prefer RAII wrappers to manual `close()`.
A leaked DRM framebuffer or an un-`munmap`ed buffer is invisible at a desk and
exhausts CMA on a box left running for a week.

### 3. The frame path
- No per-frame CPU pixel reads/writes, no per-frame heap allocation, no
  per-frame `open`/`mmap`/`ioctl` that could be done once at setup.
- Nothing blocking (disk I/O, network, logging that can stall, mutexes held
  across a wait) on the thread that services capture or page-flips.
- Every `ioctl` result checked; `EINTR`/`EAGAIN` handled where the call can
  return them; `errno` captured *before* anything that can clobber it.
- No hardcoded resolutions, refresh rates, pixel formats or stride assumptions
  where the value must come from EDID / `query_dv_timings` / the buffer
  descriptor (R2, R6).

### 4. Robustness in the field
- Signal loss, hot-plug, EDID read failure, mode-set rejection: each must
  degrade to something an operator can read (test pattern, R8) — never a
  silent black screen, never an exit without a logged reason.
- **Settings persistence is atomic.** Write-to-temp + `fsync` + `rename`; a
  power cut mid-write must leave the previous good config, not an empty file
  (R5). A config that fails to parse must fall back to defaults and say so.
- Startup must not depend on ordering luck (WiFi up before the web server,
  display connected before the app) — retry or watch, don't assume.

### 5. Hardware-touching changes
Any change to a device-tree overlay, kernel config, boot args, udev rule,
systemd unit or flashing/provisioning script is **called out explicitly in the
report**, with what happens if it's wrong (won't boot / no display / no
capture). These can brick the board or the day's test; they get a 🟡 minimum
just for existing, so the author re-reads them.

### 6. Security / config hygiene
- No WiFi passwords, hotspot PSKs, API tokens, EDID dumps from someone else's
  gear, or home network details in code, fixtures or docs.
- The web/config endpoints are on a venue network: input validated, nothing
  that lets a browser request run a shell command or write arbitrary paths.

### 7. Build and tests
- Builds cleanly with warnings as errors under the project's compiler flags.
- Tests that need the board are separated from tests that don't, and the
  host-only tests still run on a dev machine (fake DRM/V4L2 boundaries, not
  skipped suites).
- Tests assert behavior, not that a function was called.
- A new failure/cleanup branch (see §8) needs its own test.

### 8. Recurring high-cost traps (confirm each by name before calling it clean)
- **A call that returned is not an effect that happened.** `drmModeAtomicCommit`
  returning 0 is acceptance; the flip happened when the page-flip event
  arrives. `VIDIOC_STREAMON` succeeding is not frames flowing. A write to a
  config file before `fsync`/`rename` is not persisted. For every success path
  ask: *what would this report if the effect never happened?* If "success",
  it needs a completion check (event, poll with timeout, read-back) before it
  may claim done.
- **Cleanup on the failure path.** Anything set up before a later fallible
  step (buffers allocated, stream started, mode set) is torn down when that
  step fails. Trace the error branch, not just the happy path.
- **Claims about the outside world are verified, not remembered.** DRM/V4L2/
  RGA ioctl semantics, struct layouts, pixel-format FourCCs, EDID byte offsets,
  TC358743 register behaviour, `rkcif` quirks: checked against the kernel
  headers / vendor docs / the actual code path, never written from memory.
  Record what you checked and where.
- **Normalize the check, not the value.** Trim/upper-case *for* validating a
  mode string or config key; don't store or forward the altered value when the
  original is what the hardware or the user meant.

## Step 4 — Report

One prioritized report, most severe first:

- 🔴 **Critical** — bugs, leaks on the frame path, data loss, anything that
  can leave the box black or unbootable. Must fix.
- 🟡 **Warning** — real quality/maintainability problems, and every
  hardware-touching change (§5). Should fix / must re-read.
- 🔵 **Suggestion** — optional polish.

Each finding: severity, `file:line`, one-line problem, **concrete fix** (snippet
when small). Acknowledge good patterns briefly. End with a **PR-readiness
verdict**: `Clean`, `Minor polish only`, or `Has blockers` + counts. A `Clean`
verdict asserts you ran the §8 list by name and none apply — not that you
didn't look.

## Step 5 — Fix & finalize (only if the user wants)

1. Apply the 🔴/🟡 fixes (🔵 if asked), rebuild, re-run the relevant tests, and
   re-review the changed lines.
2. Draft the **PR title and body**: what changed, why, how it was verified
   (say explicitly whether it was run on the board or only host-tested), and a
   short "self-review notes" section listing what was checked.
   - Name every issue the PR fixes in the title, before the `(#PR)` suffix
     GitHub appends on squash.
   - **One closing keyword per issue in the body** — `Closes #12, closes #15.`
     GitHub links only the first number after a keyword, so `Closes #12, #15`
     auto-closes #12 alone. Use `Part of #N` for an issue the PR doesn't finish.

Do not push, open, or update a PR unless the user explicitly asks; branch off
`main` and PR back to it, per CLAUDE.md.
