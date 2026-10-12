#include "hotplug.h"

#include <cstring>
#include <stdexcept>

#include <libudev.h>

namespace ht {

HotplugMonitor::HotplugMonitor() {
  udev_ = udev_new();
  if (!udev_) throw std::runtime_error("udev_new failed");
  // "udev" source: events after udevd has processed them, so device nodes are settled.
  mon_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!mon_ || udev_monitor_filter_add_match_subsystem_devtype(mon_, "drm", nullptr) < 0 ||
      udev_monitor_enable_receiving(mon_) < 0) {
    if (mon_) udev_monitor_unref(mon_);
    udev_unref(udev_);
    throw std::runtime_error("cannot listen for DRM hotplug events");
  }
}

HotplugMonitor::~HotplugMonitor() {
  udev_monitor_unref(mon_);
  udev_unref(udev_);
}

int HotplugMonitor::fd() const { return udev_monitor_get_fd(mon_); }

bool HotplugMonitor::drain() {
  bool hotplug = false;
  // The monitor socket is non-blocking: receive until it is empty.
  while (udev_device* dev = udev_monitor_receive_device(mon_)) {
    const char* hp = udev_device_get_property_value(dev, "HOTPLUG");
    if (hp && std::strcmp(hp, "1") == 0) hotplug = true;
    udev_device_unref(dev);
  }
  return hotplug;
}

}  // namespace ht
