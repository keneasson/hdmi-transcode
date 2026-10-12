#include "capture.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <stdexcept>

#include <fcntl.h>
#include <linux/media-bus-format.h>
#include <linux/v4l2-subdev.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "capture_edid.h"

namespace ht {
namespace {

[[noreturn]] void fail(const std::string& what, int err = errno) {
  throw std::runtime_error(what + ": " + std::strerror(err));
}

int xioctl(int fd, unsigned long req, void* arg) {
  int r;
  do r = ::ioctl(fd, req, arg);
  while (r < 0 && errno == EINTR);
  return r;
}

// Device node whose sysfs name contains `needle`, e.g. "tc35874x" -> "/dev/v4l-subdev3".
std::string find_node(const std::string& needle) {
  DIR* d = opendir("/sys/class/video4linux");
  if (!d) return {};
  std::string found;
  while (dirent* e = readdir(d)) {
    if (e->d_name[0] == '.') continue;
    std::ifstream f(std::string("/sys/class/video4linux/") + e->d_name + "/name");
    std::string name;
    std::getline(f, name);
    if (name.find(needle) != std::string::npos) {
      found = std::string("/dev/") + e->d_name;
      break;
    }
  }
  closedir(d);
  return found;
}

int open_node(const std::string& path) {
  const int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) fail("open " + path);
  return fd;
}

}  // namespace

const std::vector<uint8_t>& capture_edid() { return kCaptureEdid; }

Capture::Capture() {
  const std::string bridge = find_node("tc35874x");
  const std::string video = find_node("rkisp_mainpath");
  if (bridge.empty() || video.empty())
    throw std::runtime_error(
        "capture hardware not found (TC358743 overlay enabled and the C790 connected?)");
  for (const char* n : {"rkisp-csi-subdev", "rkisp-isp-subdev"})
    isp_subdevs_.push_back(find_node(n));

  subdev_fd_ = open_node(bridge);
  try {
    video_fd_ = open_node(video);

    // R7: present our permissive EDID; sources send nothing until an EDID is loaded.
    std::vector<uint8_t> edid = capture_edid();
    v4l2_edid e{};
    e.pad = 0;
    e.start_block = 0;
    e.blocks = static_cast<uint32_t>(edid.size() / 128);
    e.edid = edid.data();
    if (xioctl(subdev_fd_, VIDIOC_S_EDID, &e) < 0) fail("load capture EDID");

    v4l2_event_subscription sub{};
    sub.type = V4L2_EVENT_SOURCE_CHANGE;
    if (xioctl(subdev_fd_, VIDIOC_SUBSCRIBE_EVENT, &sub) < 0)
      fail("subscribe to source-change events");
  } catch (...) {
    if (video_fd_ >= 0) ::close(video_fd_);
    ::close(subdev_fd_);
    throw;
  }
}

Capture::~Capture() {
  stop();
  ::close(video_fd_);
  ::close(subdev_fd_);
}

std::optional<InputTiming> Capture::detect() {
  v4l2_dv_timings t{};
  if (xioctl(subdev_fd_, VIDIOC_QUERY_DV_TIMINGS, &t) < 0) return std::nullopt;
  const v4l2_bt_timings& bt = t.bt;
  if (bt.width == 0 || bt.height == 0 || bt.pixelclock == 0) return std::nullopt;
  InputTiming in;
  in.width = static_cast<int>(bt.width);
  in.height = static_cast<int>(bt.height);
  in.interlaced = bt.interlaced != 0;
  in.pixel_clock_hz = bt.pixelclock;
  const uint64_t total = uint64_t{V4L2_DV_BT_FRAME_WIDTH(&bt)} * V4L2_DV_BT_FRAME_HEIGHT(&bt);
  if (total) in.refresh_mhz = static_cast<int>((bt.pixelclock * 1000 + total / 2) / total);
  return in;
}

void Capture::set_isp_formats(int w, int h) {
  // The ISP's sub-devices must agree with the bridge before streaming. At boot they are
  // set up for 1080p; other input sizes need them reconfigured. Failures are logged
  // rather than fatal: STREAMON reports a real mismatch.
  auto set_fmt = [&](const std::string& node, uint32_t pad, uint32_t code) {
    if (node.empty()) return;
    const int fd = ::open(node.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return;
    v4l2_subdev_format f{};
    f.which = V4L2_SUBDEV_FORMAT_ACTIVE;
    f.pad = pad;
    f.format.width = static_cast<uint32_t>(w);
    f.format.height = static_cast<uint32_t>(h);
    f.format.code = code;
    f.format.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_SUBDEV_S_FMT, &f) < 0)
      std::fprintf(stderr, "capture: %s pad %u format: %s\n", node.c_str(), pad,
                   std::strerror(errno));
    for (uint32_t target : {uint32_t{V4L2_SEL_TGT_CROP}}) {
      v4l2_subdev_selection s{};
      s.which = V4L2_SUBDEV_FORMAT_ACTIVE;
      s.pad = pad;
      s.target = target;
      s.r = {0, 0, static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
      xioctl(fd, VIDIOC_SUBDEV_S_SELECTION, &s);  // not every pad has a crop
    }
    ::close(fd);
  };
  set_fmt(isp_subdevs_[0], 0, MEDIA_BUS_FMT_UYVY8_2X8);  // csi sink
  set_fmt(isp_subdevs_[1], 0, MEDIA_BUS_FMT_UYVY8_2X8);  // isp sink
  set_fmt(isp_subdevs_[1], 2, MEDIA_BUS_FMT_YUYV8_2X8);  // isp source
}

void Capture::start(const InputTiming& in) {
  stop();

  v4l2_dv_timings t{};
  if (xioctl(subdev_fd_, VIDIOC_QUERY_DV_TIMINGS, &t) < 0) fail("input timing vanished");
  if (xioctl(subdev_fd_, VIDIOC_S_DV_TIMINGS, &t) < 0) fail("set bridge timing");
  set_isp_formats(in.width, in.height);

  v4l2_format f{};
  f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  f.fmt.pix_mp.width = static_cast<uint32_t>(in.width);
  f.fmt.pix_mp.height = static_cast<uint32_t>(in.height);
  f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
  f.fmt.pix_mp.field = V4L2_FIELD_NONE;
  if (xioctl(video_fd_, VIDIOC_S_FMT, &f) < 0) fail("set capture format");
  if (f.fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12 || f.fmt.pix_mp.num_planes != 1)
    throw std::runtime_error("ISP did not accept single-plane NV12");
  width_ = static_cast<int>(f.fmt.pix_mp.width);
  height_ = static_cast<int>(f.fmt.pix_mp.height);
  stride_ = static_cast<int>(f.fmt.pix_mp.plane_fmt[0].bytesperline);
  const uint32_t size = f.fmt.pix_mp.plane_fmt[0].sizeimage;
  // NV12 in one plane: luma rows, then half as many chroma rows at the same pitch.
  plane_height_ = stride_ ? static_cast<int>(size / stride_ * 2 / 3) : height_;
  if (plane_height_ < height_) plane_height_ = height_;

  v4l2_requestbuffers rb{};
  rb.count = 4;
  rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  rb.memory = V4L2_MEMORY_MMAP;
  if (xioctl(video_fd_, VIDIOC_REQBUFS, &rb) < 0) fail("allocate capture buffers");
  bufs_.resize(rb.count);
  try {
    for (uint32_t i = 0; i < rb.count; ++i) {
      v4l2_exportbuffer ex{};
      ex.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
      ex.index = i;
      ex.plane = 0;
      ex.flags = O_RDWR | O_CLOEXEC;
      if (xioctl(video_fd_, VIDIOC_EXPBUF, &ex) < 0) fail("export capture buffer");
      bufs_[i].dmabuf_fd = ex.fd;
      requeue(static_cast<int>(i));
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(video_fd_, VIDIOC_STREAMON, &type) < 0) fail("start capture");
  } catch (...) {
    stop();
    throw;
  }
  streaming_ = true;
  char d[64];
  std::snprintf(d, sizeof d, "%dx%d%c%.2f", in.width, in.height, in.interlaced ? 'i' : 'p',
                in.refresh_mhz / 1000.0);
  description_ = d;
}

void Capture::stop() {
  if (streaming_) {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    xioctl(video_fd_, VIDIOC_STREAMOFF, &type);
    streaming_ = false;
  }
  if (!bufs_.empty()) {
    for (auto& b : bufs_)
      if (b.dmabuf_fd >= 0) ::close(b.dmabuf_fd);
    bufs_.clear();
    v4l2_requestbuffers rb{};
    rb.count = 0;
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    rb.memory = V4L2_MEMORY_MMAP;
    xioctl(video_fd_, VIDIOC_REQBUFS, &rb);
  }
}

bool Capture::source_changed() {
  bool changed = false;
  v4l2_event ev{};
  while (xioctl(subdev_fd_, VIDIOC_DQEVENT, &ev) == 0)
    if (ev.type == V4L2_EVENT_SOURCE_CHANGE) changed = true;
  return changed;
}

std::optional<CapturedFrame> Capture::dequeue() {
  if (!streaming_) return std::nullopt;
  v4l2_plane planes[VIDEO_MAX_PLANES]{};
  v4l2_buffer b{};
  b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  b.memory = V4L2_MEMORY_MMAP;
  b.m.planes = planes;
  b.length = VIDEO_MAX_PLANES;
  if (xioctl(video_fd_, VIDIOC_DQBUF, &b) < 0) {
    if (errno == EAGAIN) return std::nullopt;
    fail("dequeue capture buffer");
  }
  if (b.flags & V4L2_BUF_FLAG_ERROR) {
    requeue(static_cast<int>(b.index));
    return std::nullopt;
  }
  CapturedFrame fr;
  fr.index = static_cast<int>(b.index);
  fr.dmabuf_fd = bufs_.at(b.index).dmabuf_fd;
  fr.width = width_;
  fr.height = height_;
  fr.stride_bytes = stride_;
  fr.plane_height = plane_height_;
  fr.sequence = b.sequence;
  return fr;
}

void Capture::requeue(int index) {
  v4l2_plane planes[VIDEO_MAX_PLANES]{};
  v4l2_buffer b{};
  b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  b.memory = V4L2_MEMORY_MMAP;
  b.index = static_cast<uint32_t>(index);
  b.m.planes = planes;
  b.length = 1;
  if (xioctl(video_fd_, VIDIOC_QBUF, &b) < 0) fail("queue capture buffer");
}

}  // namespace ht
