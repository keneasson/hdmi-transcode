// DRM/KMS output on the board's HDMI connector (requirements R1, R2, R2b, N4, N6).
//
// Owns: the DRM device fd (and DRM master), the scanout buffers + framebuffers, and the
// CRTC it programs. The CRTC state found at open is restored on destruction, so a clean
// exit leaves the console as it was.
//
// Frames are shown with page flips on vblank from a small ring of buffers: a buffer is
// only rendered into when it is neither on screen nor queued to go on screen, so the
// display never shows a half-written frame. When frames arrive faster than the display
// refreshes, the newest one wins (latency over completeness).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <xf86drmMode.h>

#include "mode_select.h"
#include "test_pattern.h"

namespace ht {

class DrmOutput {
 public:
  static constexpr int kBuffers = 3;

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

  // Allocates kBuffers XRGB8888 scanout buffers for `mode` (cleared to black) and
  // programs the CRTC with buffer 0. The mode need not be in modes() (built-in fallback
  // timings for displays without a usable EDID).
  void set_mode(const drmModeModeInfo& mode);

  // Turns the CRTC off and frees the scanout buffers (the display went away).
  void blank();
  bool active() const { return !bufs_.empty(); }
  const drmModeModeInfo& mode() const { return mode_; }

  // --- frame pacing (valid while active()) -------------------------------------------
  // A buffer that is safe to render into now.
  int acquire() const;
  // Shows buffer `i` at the next vblank. If a flip is already in flight, `i` replaces any
  // frame still waiting behind it.
  void present(int i);
  // Poll this for POLLIN and call handle_events() to complete flips.
  int fd() const { return fd_; }
  void handle_events();

  // CPU view of buffer `i`, and the same memory as a DMA-BUF for the RGA.
  Surface surface(int i);
  int dmabuf_fd(int i) const { return bufs_.at(i).prime_fd; }
  int stride_bytes(int i) const { return static_cast<int>(bufs_.at(i).pitch); }

  // Forces the connector's DPMS to On (N6: the output never sleeps).
  void keep_awake();

 private:
  struct Buffer {
    uint32_t handle = 0, fb_id = 0, pitch = 0;
    uint64_t size = 0;
    void* map = nullptr;
    int prime_fd = -1;
  };

  void destroy_buffers();
  void read_edid();
  void take_connector_state(drmModeConnector* c);
  void submit_flip(int i);
  static void on_flip(int fd, unsigned seq, unsigned sec, unsigned usec, void* self);

  int fd_ = -1;
  uint32_t connector_id_ = 0;
  bool connected_ = false;
  uint32_t crtc_id_ = 0;
  std::vector<drmModeModeInfo> modes_;
  std::vector<uint8_t> edid_;
  drmModeModeInfo mode_{};
  drmModeCrtc* saved_crtc_ = nullptr;

  std::vector<Buffer> bufs_;
  int front_ = -1;    // on screen
  int pending_ = -1;  // flip submitted, waiting for vblank
  int next_ = -1;     // rendered, waiting for the pending flip to finish
};

}  // namespace ht
