// Display hot-plug notifications (R2b): the kernel reports connector changes as a udev
// "change" event with HOTPLUG=1 on the DRM card. A forced change through
// /sys/class/drm/<card>-<connector>/status produces the same event, which is how this is
// tested without touching the cable.
#pragma once

struct udev;
struct udev_monitor;

namespace ht {

class HotplugMonitor {
 public:
  // Throws std::runtime_error if udev is unavailable.
  HotplugMonitor();
  ~HotplugMonitor();
  HotplugMonitor(const HotplugMonitor&) = delete;
  HotplugMonitor& operator=(const HotplugMonitor&) = delete;

  // Non-blocking fd to poll() for readability.
  int fd() const;

  // Reads every pending event; returns true if at least one was a DRM hotplug.
  bool drain();

 private:
  udev* udev_ = nullptr;
  udev_monitor* mon_ = nullptr;
};

}  // namespace ht
