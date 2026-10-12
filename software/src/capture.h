// HDMI capture through the TC358743 bridge and the RK3566 ISP (requirements R6, R7, R8,
// N8). Owns the bridge sub-device and the ISP video node for the life of the process.
//
// N8 (robust restarts): the video node is opened once and never re-opened; a restart is
// always STREAMOFF + free every buffer before anything new is set up. Overlapping a
// close and a re-open of this node crashed the Rockchip ISP driver on 2026-10-11.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ht {

struct InputTiming {
  int width = 0, height = 0;
  int refresh_mhz = 0;  // 59940 for 59.94 Hz
  bool interlaced = false;
  uint64_t pixel_clock_hz = 0;
};

struct CapturedFrame {
  int index = -1;       // give back with requeue()
  int dmabuf_fd = -1;   // NV12, valid until requeue()
  int width = 0, height = 0;
  int stride_bytes = 0;
  int plane_height = 0;  // rows before the chroma plane starts
  uint32_t sequence = 0;
};

class Capture {
 public:
  // Finds the bridge and ISP nodes, opens them, loads our EDID (R7) and subscribes to
  // source-change events. Throws std::runtime_error if the capture hardware is missing.
  Capture();
  ~Capture();
  Capture(const Capture&) = delete;
  Capture& operator=(const Capture&) = delete;

  // Poll subdev_fd() for POLLPRI (input changed) and video_fd() for POLLIN (frame ready).
  int subdev_fd() const { return subdev_fd_; }
  int video_fd() const { return video_fd_; }

  // Current input timing, or nullopt with no (stable) signal.
  std::optional<InputTiming> detect();

  // Configures the bridge and ISP for `t` and starts streaming NV12.
  void start(const InputTiming& t);
  // STREAMOFF and free all buffers. Safe to call when stopped.
  void stop();
  bool streaming() const { return streaming_; }

  // Drains pending source-change events; true if any arrived.
  bool source_changed();

  // Next filled buffer, or nullopt if none is ready (non-blocking).
  std::optional<CapturedFrame> dequeue();
  void requeue(int index);

  const std::string& description() const { return description_; }

 private:
  struct Buf {
    int dmabuf_fd = -1;
  };
  void set_isp_formats(int width, int height);

  int subdev_fd_ = -1;
  int video_fd_ = -1;
  std::vector<std::string> isp_subdevs_;  // "/dev/v4l-subdevN" for rkisp-isp / rkisp-csi
  std::vector<Buf> bufs_;
  bool streaming_ = false;
  int width_ = 0, height_ = 0, stride_ = 0, plane_height_ = 0;
  std::string description_;
};

// The EDID the capture side presents upstream (R7).
const std::vector<uint8_t>& capture_edid();

}  // namespace ht
