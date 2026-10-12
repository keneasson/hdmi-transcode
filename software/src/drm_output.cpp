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

int refresh_hz(const drmModeModeInfo& m) {
  if (m.htotal == 0 || m.vtotal == 0) return static_cast<int>(m.vrefresh);
  return static_cast<int>((m.clock * 1000.0) / (double(m.htotal) * m.vtotal) + 0.5);
}

}  // namespace

std::optional<ModeRequest> parse_mode(const std::string& s) {
  ModeRequest r;
  char tail = 0;
  if (std::sscanf(s.c_str(), "%dx%d@%d%c", &r.width, &r.height, &r.refresh, &tail) == 3 ||
      std::sscanf(s.c_str(), "%dx%d%c", &r.width, &r.height, &tail) == 2) {
    if (r.width > 0 && r.height > 0 && r.refresh >= 0) return r;
  }
  return std::nullopt;
}

std::string DrmOutput::describe(const drmModeModeInfo& m) {
  return std::to_string(m.hdisplay) + "x" + std::to_string(m.vdisplay) + " @ " +
         std::to_string(refresh_hz(m)) + "Hz";
}

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
          c->connection == DRM_MODE_CONNECTED && c->count_modes > 0)
        conn = std::move(c);
    }
    if (!conn) throw std::runtime_error("no connected HDMI display with modes");
    connector_id_ = conn->connector_id;
    modes_.assign(conn->modes, conn->modes + conn->count_modes);

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

const drmModeModeInfo& DrmOutput::set_mode(const ModeRequest& req) {
  const drmModeModeInfo* pick = nullptr;
  if (req.width)
    for (const auto& m : modes_)
      if (m.hdisplay == req.width && m.vdisplay == req.height &&
          (!req.refresh || refresh_hz(m) == req.refresh) &&
          !(m.flags & DRM_MODE_FLAG_INTERLACE)) {
        pick = &m;
        break;
      }
  if (!pick)
    for (const auto& m : modes_)
      if (m.type & DRM_MODE_TYPE_PREFERRED) {
        pick = &m;
        break;
      }
  if (!pick) pick = &modes_.front();
  if (req.width && (pick->hdisplay != req.width || pick->vdisplay != req.height))
    std::fprintf(stderr, "requested %dx%d not offered by the display; using %s\n",
                 req.width, req.height, describe(*pick).c_str());
  mode_ = *pick;

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
  return mode_;
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
