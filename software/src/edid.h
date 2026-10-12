// Minimal EDID reader: just enough to find the display's own preferred timing (R2).
// The kernel's "preferred" flag can't be trusted for this: on the Rockchip BSP kernel
// a SyncMaster 2433 (EDID preferred 1920x1200@59.95) is reported with 1080p50 flagged
// preferred instead (docs/hardware-notes.md, 2026-10-11).
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace ht {

struct EdidTiming {
  int width = 0;
  int height = 0;
  int pixel_clock_khz = 0;
  int htotal = 0;
  int vtotal = 0;
  bool interlaced = false;

  // Refresh in millihertz (59950 for 59.95 Hz); 0 if totals are unknown.
  int refresh_mhz() const;
};

// Returns the first detailed timing descriptor of the base block (EDID 1.3+ defines it
// as the preferred timing), or nullopt if the blob is too short, the header is wrong,
// the base-block checksum fails, or no detailed timing is present. Never throws.
std::optional<EdidTiming> edid_preferred_timing(const std::vector<uint8_t>& edid);

}  // namespace ht
