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

#include "mode_select.h"
#include "test_pattern.h"

namespace ht {

class DrmOutput {
 public:
  // Opens the DRM card, becomes master and takes its HDMI-A connector, with or without a
  // display attached (R2b). Throws std::runtime_error with a reason on failure (no such
  // card or connector, not master, ...).
  explicit DrmOutput(const std::string& device = "/dev/dri/card0");
  ~DrmOutput();
  DrmOutput(const DrmOutput&) = delete;
  DrmOutput& operator=(const DrmOutput&) = delete;

  // Re-detects the display (call on a hotplug event); refreshes modes() and edid().
  // Returns connected().
  bool probe();
  bool connected() const { return connected_; }

  // Modes the display offers, in connector order. Empty when disconnected, and possibly
  // when connected (no or broken EDID).
  const std::vector<drmModeModeInfo>& modes() const { return modes_; }

  // The connector's raw EDID blob; empty if the display provided none.
  const std::vector<uint8_t>& edid() const { return edid_; }

  // Allocates a scanout buffer for `mode` and programs the CRTC. The mode need not be in
  // modes() (built-in fallback timings for displays without a usable EDID).
  void set_mode(const drmModeModeInfo& mode);

  // Turns the CRTC off and frees the scanout buffer (the display went away).
  void blank();
  bool active() const { return fb_id_ != 0; }
  const drmModeModeInfo& mode() const { return mode_; }

  // CPU view of the scanout buffer; valid after set_mode().
  Surface surface();

  // Forces the connector's DPMS to On (N6: the output never sleeps).
  void keep_awake();

 private:
  void destroy_buffer();
  void read_edid();
  void take_connector_state(drmModeConnector* c);

  int fd_ = -1;
  uint32_t connector_id_ = 0;
  bool connected_ = false;
  uint32_t crtc_id_ = 0;
  std::vector<drmModeModeInfo> modes_;
  std::vector<uint8_t> edid_;
  drmModeModeInfo mode_{};
  drmModeCrtc* saved_crtc_ = nullptr;

  uint32_t handle_ = 0, fb_id_ = 0, pitch_ = 0;
  uint64_t size_ = 0;
  void* map_ = nullptr;
};

}  // namespace ht
