// Output mode choice (R2, R2a). Pure logic over the connector's mode list and the
// display's EDID, so it is unit-testable without a display.
//
// Order:
//   1. an explicit request (command line now, saved setting later), if the display
//      offers it, or if it is one of our built-in fallback timings (forced);
//   2. the display's own preferred timing, read from its EDID (not the kernel's flag);
//   3. the fallback ladder 1280x800@60 -> 1024x768@60 -> 640x480@60, from the offered
//      list;
//   4. anything progressive the display offers;
//   5. no usable EDID / no modes at all: the built-in 1280x800@60 timing.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include <xf86drmMode.h>

#include "edid.h"

namespace ht {

struct ModeRequest {
  int width = 0;
  int height = 0;
  int refresh = 0;  // Hz, 0 = any
};

// Parses "1280x800" or "1280x800@60". Returns nullopt on malformed input.
std::optional<ModeRequest> parse_mode(const std::string& s);

struct ModeChoice {
  drmModeModeInfo mode;
  std::string reason;  // human-readable, for logs and the config UI
};

ModeChoice choose_mode(const std::vector<drmModeModeInfo>& offered,
                       const std::optional<EdidTiming>& edid_preferred,
                       const std::optional<ModeRequest>& request);

// Built-in VESA timings for the fallback ladder, in ladder order.
const std::vector<drmModeModeInfo>& fallback_modes();

// True if both describe the same signal on the wire (size, clock, porches, sync, flags),
// i.e. switching between them would not need a new mode-set.
bool same_timing(const drmModeModeInfo& a, const drmModeModeInfo& b);

// Refresh in millihertz computed from the timing (vrefresh is rounded to whole Hz).
int refresh_mhz(const drmModeModeInfo& m);
std::string describe(const drmModeModeInfo& m);

}  // namespace ht
