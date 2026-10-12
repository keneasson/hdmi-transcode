// Tests for EDID parsing and the R2/R2a mode choice. No display needed.
#include <cstdio>
#include <optional>
#include <vector>

#include "check.h"
#include "capture.h"
#include "edid.h"
#include "mode_select.h"
#include "syncmaster2433_edid.h"

namespace {

drmModeModeInfo mode(int w, int h, uint32_t clock, int ht, int vt, bool kernel_pref = false,
                     bool interlaced = false) {
  drmModeModeInfo m{};
  m.hdisplay = static_cast<uint16_t>(w);
  m.vdisplay = static_cast<uint16_t>(h);
  m.clock = clock;
  m.htotal = static_cast<uint16_t>(ht);
  m.vtotal = static_cast<uint16_t>(vt);
  if (kernel_pref) m.type |= DRM_MODE_TYPE_PREFERRED;
  if (interlaced) m.flags |= DRM_MODE_FLAG_INTERLACE;
  return m;
}

// The SyncMaster 2433's list as the board's kernel reports it (2026-10-11), abridged:
// 1080p50 flagged preferred, the EDID's real preferred 1920x1200@59.95 second.
std::vector<drmModeModeInfo> syncmaster_modes() {
  return {mode(1920, 1080, 148500, 2640, 1125, true), mode(1920, 1200, 154000, 2080, 1235),
          mode(1280, 800, 83500, 1680, 831),          mode(1024, 768, 65000, 1344, 806),
          mode(640, 480, 25175, 800, 525)};
}

void edid_syncmaster_preferred_is_1920x1200() {
  auto t = ht::edid_preferred_timing(kSyncMaster2433Edid);
  CHECK(t.has_value());
  if (!t) return;
  CHECK(t->width == 1920 && t->height == 1200);
  CHECK(t->pixel_clock_khz == 154000);
  CHECK(t->refresh_mhz() == 59950);
  CHECK(!t->interlaced);
}

void edid_rejects_garbage() {
  CHECK(!ht::edid_preferred_timing({}));
  std::vector<uint8_t> e = kSyncMaster2433Edid;
  e.resize(127);
  CHECK(!ht::edid_preferred_timing(e));  // short
  e = kSyncMaster2433Edid;
  e[0] = 0x01;
  CHECK(!ht::edid_preferred_timing(e));  // bad header
  e = kSyncMaster2433Edid;
  e[60] ^= 0x10;
  CHECK(!ht::edid_preferred_timing(e));  // bad checksum
  e = kSyncMaster2433Edid;
  for (int off = 54; off <= 108; off += 18) e[off] = e[off + 1] = 0;  // no timings
  uint8_t sum = 0;
  for (int i = 0; i < 127; ++i) sum = static_cast<uint8_t>(sum + e[i]);
  e[127] = static_cast<uint8_t>(-sum);
  CHECK(!ht::edid_preferred_timing(e));
}

void choice_uses_edid_not_kernel_flag() {
  auto c = ht::choose_mode(syncmaster_modes(), ht::edid_preferred_timing(kSyncMaster2433Edid),
                           std::nullopt);
  CHECK(c.mode.hdisplay == 1920 && c.mode.vdisplay == 1200);
  CHECK(c.reason == "display's preferred mode (EDID)");
}

void request_wins_when_offered() {
  auto c = ht::choose_mode(syncmaster_modes(), ht::edid_preferred_timing(kSyncMaster2433Edid),
                           ht::ModeRequest{1280, 800, 60});
  CHECK(c.mode.hdisplay == 1280 && c.mode.vdisplay == 800);
  CHECK(c.reason == "requested");
}

void request_for_builtin_timing_is_forced() {
  // A display with no EDID offers nothing; 1024x768 is a built-in timing.
  auto c = ht::choose_mode({}, std::nullopt, ht::ModeRequest{1024, 768, 60});
  CHECK(c.mode.hdisplay == 1024 && c.mode.vdisplay == 768 && c.mode.clock == 65000);
  CHECK(c.reason.rfind("requested (built-in", 0) == 0);
}

void impossible_request_falls_back_to_automatic() {
  auto c = ht::choose_mode(syncmaster_modes(), ht::edid_preferred_timing(kSyncMaster2433Edid),
                           ht::ModeRequest{3840, 2160, 60});
  CHECK(c.mode.hdisplay == 1920 && c.mode.vdisplay == 1200);
}

void ladder_when_edid_unusable() {
  auto c = ht::choose_mode(syncmaster_modes(), std::nullopt, std::nullopt);
  CHECK(c.mode.hdisplay == 1280 && c.mode.vdisplay == 800);
  CHECK(c.reason == "fallback ladder");
  // 1280x800 missing: next rung.
  std::vector<drmModeModeInfo> no800 = {mode(1920, 1080, 148500, 2200, 1125),
                                        mode(1024, 768, 65000, 1344, 806)};
  c = ht::choose_mode(no800, std::nullopt, std::nullopt);
  CHECK(c.mode.hdisplay == 1024);
}

void ladder_skips_wrong_refresh_and_interlaced() {
  std::vector<drmModeModeInfo> m = {mode(1280, 800, 71000, 1440, 823 * 2, false, false),  // ~30 Hz
                                    mode(1024, 768, 65000, 1344, 806, false, true),
                                    mode(640, 480, 25175, 800, 525)};
  auto c = ht::choose_mode(m, std::nullopt, std::nullopt);
  CHECK(c.mode.hdisplay == 640);
}

void no_modes_at_all_gives_builtin_1280x800() {
  auto c = ht::choose_mode({}, std::nullopt, std::nullopt);
  CHECK(c.mode.hdisplay == 1280 && c.mode.vdisplay == 800 && c.mode.clock == 83500);
  CHECK(ht::refresh_mhz(c.mode) > 59500 && ht::refresh_mhz(c.mode) < 60500);
}

void fallback_timings_are_60hz() {
  for (const auto& m : ht::fallback_modes()) {
    const int mhz = ht::refresh_mhz(m);
    CHECK(mhz > 59500 && mhz < 60500);
    CHECK(m.vrefresh == 60);
    CHECK(m.hsync_start > m.hdisplay && m.hsync_end > m.hsync_start && m.htotal > m.hsync_end);
    CHECK(m.vsync_start > m.vdisplay && m.vsync_end > m.vsync_start && m.vtotal > m.vsync_end);
  }
}

void describe_formats_fractional_rates() {
  CHECK(ht::describe(mode(1920, 1200, 154000, 2080, 1235)) == "1920x1200 @ 59.95Hz");
  CHECK(ht::describe(mode(1920, 1080, 148500, 2200, 1125)) == "1920x1080 @ 60Hz");
}

void same_timing_cases() {
  const auto a = mode(1920, 1200, 154000, 2080, 1235);
  auto b = a;
  CHECK(ht::same_timing(a, b));
  b.type = DRM_MODE_TYPE_USERDEF;  // type/name are bookkeeping, not signal
  std::snprintf(b.name, sizeof b.name, "other");
  CHECK(ht::same_timing(a, b));
  b = a;
  b.clock = 148500;
  CHECK(!ht::same_timing(a, b));
  b = a;
  b.flags |= DRM_MODE_FLAG_NVSYNC;
  CHECK(!ht::same_timing(a, b));
}

void capture_edid_is_valid_and_prefers_1080p60() {
  const auto& e = ht::capture_edid();
  CHECK(e.size() == 256);
  for (int block = 0; block < 2; ++block) {
    uint8_t sum = 0;
    for (int i = 0; i < 128; ++i) sum = static_cast<uint8_t>(sum + e[block * 128 + i]);
    CHECK(sum == 0);
  }
  CHECK(e[126] == 1);     // one extension block
  CHECK(e[128] == 0x02);  // CTA-861
  auto t = ht::edid_preferred_timing(e);
  CHECK(t && t->width == 1920 && t->height == 1080 && t->refresh_mhz() == 60000);
}

void parse_mode_cases() {
  auto a = ht::parse_mode("1280x800@60");
  CHECK(a && a->width == 1280 && a->height == 800 && a->refresh == 60);
  auto b = ht::parse_mode("1024x768");
  CHECK(b && b->width == 1024 && b->height == 768 && b->refresh == 0);
  CHECK(!ht::parse_mode("1024"));
  CHECK(!ht::parse_mode("1024x768@60x"));
  CHECK(!ht::parse_mode("1024x768@"));
  CHECK(!ht::parse_mode("0x768"));
  CHECK(!ht::parse_mode("garbage"));
}

}  // namespace

int main() {
  edid_syncmaster_preferred_is_1920x1200();
  edid_rejects_garbage();
  choice_uses_edid_not_kernel_flag();
  request_wins_when_offered();
  request_for_builtin_timing_is_forced();
  impossible_request_falls_back_to_automatic();
  ladder_when_edid_unusable();
  ladder_skips_wrong_refresh_and_interlaced();
  no_modes_at_all_gives_builtin_1280x800();
  fallback_timings_are_60hz();
  describe_formats_fractional_rates();
  same_timing_cases();
  capture_edid_is_valid_and_prefers_1080p60();
  parse_mode_cases();
  return finish();
}
