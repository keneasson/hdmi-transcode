#include "mode_select.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ht {
namespace {

drmModeModeInfo make_mode(uint32_t clock_khz, uint16_t hd, uint16_t hss, uint16_t hse,
                          uint16_t ht, uint16_t vd, uint16_t vss, uint16_t vse, uint16_t vt,
                          uint32_t flags) {
  drmModeModeInfo m{};
  m.clock = clock_khz;
  m.hdisplay = hd;
  m.hsync_start = hss;
  m.hsync_end = hse;
  m.htotal = ht;
  m.vdisplay = vd;
  m.vsync_start = vss;
  m.vsync_end = vse;
  m.vtotal = vt;
  m.flags = flags;
  m.type = DRM_MODE_TYPE_USERDEF;
  m.vrefresh = static_cast<uint32_t>((refresh_mhz(m) + 500) / 1000);
  std::snprintf(m.name, sizeof m.name, "%ux%u", hd, vd);
  return m;
}

bool progressive(const drmModeModeInfo& m) { return !(m.flags & DRM_MODE_FLAG_INTERLACE); }

bool same_size(const drmModeModeInfo& m, int w, int h) {
  return m.hdisplay == w && m.vdisplay == h;
}

// Refresh match within 0.5 Hz: 59.94 matches a request for 60, 50 does not.
bool refresh_matches(const drmModeModeInfo& m, int hz) {
  return hz == 0 || std::abs(refresh_mhz(m) - hz * 1000) <= 500;
}

const drmModeModeInfo* find(const std::vector<drmModeModeInfo>& offered, int w, int h,
                            int hz) {
  for (const auto& m : offered)
    if (progressive(m) && same_size(m, w, h) && refresh_matches(m, hz)) return &m;
  return nullptr;
}

}  // namespace

int refresh_mhz(const drmModeModeInfo& m) {
  if (m.htotal == 0 || m.vtotal == 0) return static_cast<int>(m.vrefresh) * 1000;
  return static_cast<int>(m.clock * 1e6 / (double(m.htotal) * m.vtotal) + 0.5);
}

std::string describe(const drmModeModeInfo& m) {
  const int mhz = refresh_mhz(m);
  char hz[16];
  if (mhz % 1000 == 0)
    std::snprintf(hz, sizeof hz, "%d", mhz / 1000);
  else
    std::snprintf(hz, sizeof hz, "%.2f", mhz / 1000.0);
  return std::to_string(m.hdisplay) + "x" + std::to_string(m.vdisplay) + " @ " + hz + "Hz";
}

std::optional<ModeRequest> parse_mode(const std::string& s) {
  ModeRequest r;
  char tail = 0;
  if (std::sscanf(s.c_str(), "%dx%d@%d%c", &r.width, &r.height, &r.refresh, &tail) == 3 ||
      std::sscanf(s.c_str(), "%dx%d%c", &r.width, &r.height, &tail) == 2) {
    if (r.width > 0 && r.height > 0 && r.refresh >= 0) return r;
  }
  return std::nullopt;
}

const std::vector<drmModeModeInfo>& fallback_modes() {
  // VESA DMT timings: 1280x800@60 (CVT, DMT 0x1C), 1024x768@60 (0x10), 640x480@60 (0x04).
  static const std::vector<drmModeModeInfo> kModes = {
      make_mode(83500, 1280, 1352, 1480, 1680, 800, 803, 809, 831,
                DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC),
      make_mode(65000, 1024, 1048, 1184, 1344, 768, 771, 777, 806,
                DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
      make_mode(25175, 640, 656, 752, 800, 480, 490, 492, 525,
                DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC),
  };
  return kModes;
}

ModeChoice choose_mode(const std::vector<drmModeModeInfo>& offered,
                       const std::optional<EdidTiming>& edid_preferred,
                       const std::optional<ModeRequest>& request) {
  if (request) {
    if (auto* m = find(offered, request->width, request->height, request->refresh))
      return {*m, "requested"};
    if (auto* m = find(fallback_modes(), request->width, request->height, request->refresh))
      return {*m, "requested (built-in timing; not in the display's EDID)"};
    // Not something we can produce: fall through to the automatic choice.
  }

  if (edid_preferred && !edid_preferred->interlaced) {
    const int hz = (edid_preferred->refresh_mhz() + 500) / 1000;
    if (auto* m = find(offered, edid_preferred->width, edid_preferred->height, hz))
      return {*m, "display's preferred mode (EDID)"};
  }

  for (const auto& f : fallback_modes())
    if (auto* m = find(offered, f.hdisplay, f.vdisplay, 60))
      return {*m, "fallback ladder"};

  for (const auto& m : offered)
    if (progressive(m)) return {m, "first progressive mode the display offers"};

  return {fallback_modes().front(), "no usable EDID: built-in 1280x800@60"};
}

}  // namespace ht
