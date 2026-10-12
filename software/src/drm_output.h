// DRM/KMS output on the board's HDMI connector (requirements R1, R2, N4, N6).
//
// Owns: the DRM device fd (and DRM master), one dumb scanout buffer + framebuffer, and
// the CRTC it programs. The CRTC state found at open is restored on destruction, so a
// crash-free exit leaves the console as it was.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <xf86drmMode.h>

#include "test_pattern.h"

namespace ht {

struct ModeRequest {
  int width = 0;      // 0 = use the display's preferred mode
  int height = 0;
  int refresh = 0;    // 0 = any
};

// Parses "1280x800" or "1280x800@60". Returns nullopt on malformed input.
std::optional<ModeRequest> parse_mode(const std::string& s);

class DrmOutput {
 public:
  // Opens the first DRM card that has a connected HDMI-A connector. Throws
  // std::runtime_error with a reason on failure (no display, not master, ...).
  explicit DrmOutput(const std::string& device = "/dev/dri/card0");
  ~DrmOutput();
  DrmOutput(const DrmOutput&) = delete;
  DrmOutput& operator=(const DrmOutput&) = delete;

  // Modes the display offers, in connector order (preferred first on most sinks).
  const std::vector<drmModeModeInfo>& modes() const { return modes_; }

  // Picks a mode (exact match for `req`, else preferred, else first), allocates a
  // scanout buffer for it and programs the CRTC. Returns the mode used.
  const drmModeModeInfo& set_mode(const ModeRequest& req);

  // CPU view of the scanout buffer; valid after set_mode().
  Surface surface();

  // Forces the connector's DPMS to On (N6: the output never sleeps).
  void keep_awake();

  static std::string describe(const drmModeModeInfo& m);

 private:
  void destroy_buffer();

  int fd_ = -1;
  uint32_t connector_id_ = 0;
  uint32_t crtc_id_ = 0;
  std::vector<drmModeModeInfo> modes_;
  drmModeModeInfo mode_{};
  drmModeCrtc* saved_crtc_ = nullptr;

  uint32_t handle_ = 0, fb_id_ = 0, pitch_ = 0;
  uint64_t size_ = 0;
  void* map_ = nullptr;
};

}  // namespace ht
