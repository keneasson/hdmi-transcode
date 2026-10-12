#include "drm_output.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <drm_fourcc.h>
#include <xf86drm.h>

namespace ht {
namespace {

[[noreturn]] void fail(const std::string& what, int err = errno) {
  throw std::runtime_error(what + ": " + std::strerror(err));
}

// Smart pointers for libdrm's C objects.
struct ResFree { void operator()(drmModeRes* p) const { drmModeFreeResources(p); } };
struct ConnFree { void operator()(drmModeConnector* p) const { drmModeFreeConnector(p); } };
struct EncFree { void operator()(drmModeEncoder* p) const { drmModeFreeEncoder(p); } };
using ResPtr = std::unique_ptr<drmModeRes, ResFree>;
using ConnPtr = std::unique_ptr<drmModeConnector, ConnFree>;
using EncPtr = std::unique_ptr<drmModeEncoder, EncFree>;

}  // namespace

DrmOutput::DrmOutput(const std::string& device) {
  fd_ = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
  if (fd_ < 0) fail("open " + device);
  try {
    // We must be the only one driving the display (N4). A display manager or another
    // instance holding master is reported clearly instead of fighting it.
    if (drmSetMaster(fd_) != 0)
      fail("drmSetMaster (is a display manager or another instance running?)");

    ResPtr res(drmModeGetResources(fd_));
    if (!res) fail("drmModeGetResources");

    ConnPtr conn;
    for (int i = 0; i < res->count_connectors && !conn; ++i) {
      ConnPtr c(drmModeGetConnector(fd_, res->connectors[i]));
      if (c && c->connector_type == DRM_MODE_CONNECTOR_HDMIA &&
          c->connection == DRM_MODE_CONNECTED)
        conn = std::move(c);
    }
    if (!conn) throw std::runtime_error("no connected HDMI display");
    connector_id_ = conn->connector_id;
    modes_.assign(conn->modes, conn->modes + conn->count_modes);
    read_edid();

    // Prefer the CRTC already driving this connector; otherwise any CRTC the
    // connector's encoders can reach.
    if (conn->encoder_id) {
      EncPtr enc(drmModeGetEncoder(fd_, conn->encoder_id));
      if (enc && enc->crtc_id) crtc_id_ = enc->crtc_id;
    }
    for (int e = 0; e < conn->count_encoders && !crtc_id_; ++e) {
      EncPtr enc(drmModeGetEncoder(fd_, conn->encoders[e]));
      if (!enc) continue;
      for (int c = 0; c < res->count_crtcs; ++c)
        if (enc->possible_crtcs & (1u << c)) {
          crtc_id_ = res->crtcs[c];
          break;
        }
    }
    if (!crtc_id_) throw std::runtime_error("no CRTC available for the HDMI connector");
    saved_crtc_ = drmModeGetCrtc(fd_, crtc_id_);
  } catch (...) {
    ::close(fd_);
    throw;
  }
}

DrmOutput::~DrmOutput() {
  if (saved_crtc_) {
    // Best effort: put back whatever was on screen before us.
    drmModeSetCrtc(fd_, saved_crtc_->crtc_id, saved_crtc_->buffer_id, saved_crtc_->x,
                   saved_crtc_->y, &connector_id_, 1,
                   saved_crtc_->mode_valid ? &saved_crtc_->mode : nullptr);
    drmModeFreeCrtc(saved_crtc_);
  }
  destroy_buffer();
  if (fd_ >= 0) {
    drmDropMaster(fd_);
    ::close(fd_);
  }
}

void DrmOutput::destroy_buffer() {
  if (map_) munmap(map_, size_);
  map_ = nullptr;
  if (fb_id_) drmModeRmFB(fd_, fb_id_);
  fb_id_ = 0;
  if (handle_) {
    drm_mode_destroy_dumb d{};
    d.handle = handle_;
    drmIoctl(fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
  }
  handle_ = 0;
}

void DrmOutput::set_mode(const drmModeModeInfo& mode) {
  mode_ = mode;

  destroy_buffer();
  drm_mode_create_dumb create{};
  create.width = mode_.hdisplay;
  create.height = mode_.vdisplay;
  create.bpp = 32;
  if (drmIoctl(fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) fail("create dumb buffer");
  handle_ = create.handle;
  pitch_ = create.pitch;
  size_ = create.size;

  const uint32_t handles[4] = {handle_}, pitches[4] = {pitch_}, offsets[4] = {0};
  if (drmModeAddFB2(fd_, mode_.hdisplay, mode_.vdisplay, DRM_FORMAT_XRGB8888, handles,
                    pitches, offsets, &fb_id_, 0) != 0)
    fail("drmModeAddFB2");

  drm_mode_map_dumb map{};
  map.handle = handle_;
  if (drmIoctl(fd_, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) fail("map dumb buffer");
  map_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, map.offset);
  if (map_ == MAP_FAILED) {
    map_ = nullptr;
    fail("mmap dumb buffer");
  }
  std::memset(map_, 0, size_);

  if (drmModeSetCrtc(fd_, crtc_id_, fb_id_, 0, 0, &connector_id_, 1, &mode_) != 0)
    fail("drmModeSetCrtc " + describe(mode_));
}

void DrmOutput::read_edid() {
  drmModeObjectProperties* props =
      drmModeObjectGetProperties(fd_, connector_id_, DRM_MODE_OBJECT_CONNECTOR);
  if (!props) return;
  for (uint32_t i = 0; i < props->count_props && edid_.empty(); ++i) {
    drmModePropertyRes* p = drmModeGetProperty(fd_, props->props[i]);
    if (p && std::strcmp(p->name, "EDID") == 0 && props->prop_values[i]) {
      if (drmModePropertyBlobRes* b =
              drmModeGetPropertyBlob(fd_, static_cast<uint32_t>(props->prop_values[i]))) {
        const auto* d = static_cast<const uint8_t*>(b->data);
        edid_.assign(d, d + b->length);
        drmModeFreePropertyBlob(b);
      }
    }
    drmModeFreeProperty(p);
  }
  drmModeFreeObjectProperties(props);
}

Surface DrmOutput::surface() {
  if (!map_) throw std::logic_error("surface() before set_mode()");
  return Surface{static_cast<uint32_t*>(map_), mode_.hdisplay, mode_.vdisplay, pitch_ / 4};
}

void DrmOutput::keep_awake() {
  drmModeObjectProperties* props =
      drmModeObjectGetProperties(fd_, connector_id_, DRM_MODE_OBJECT_CONNECTOR);
  if (!props) return;
  for (uint32_t i = 0; i < props->count_props; ++i) {
    drmModePropertyRes* p = drmModeGetProperty(fd_, props->props[i]);
    if (p && std::strcmp(p->name, "DPMS") == 0)
      drmModeConnectorSetProperty(fd_, connector_id_, p->prop_id, DRM_MODE_DPMS_ON);
    drmModeFreeProperty(p);
  }
  drmModeFreeObjectProperties(props);
}

}  // namespace ht
