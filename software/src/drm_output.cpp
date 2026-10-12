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

    // The board has one HDMI-A connector. Take it whether or not a display is attached
    // yet: displays come and go (R2b), probe() tracks that.
    ConnPtr conn;
    for (int i = 0; i < res->count_connectors && !conn; ++i) {
      ConnPtr c(drmModeGetConnector(fd_, res->connectors[i]));
      if (c && c->connector_type == DRM_MODE_CONNECTOR_HDMIA) conn = std::move(c);
    }
    if (!conn) throw std::runtime_error("no HDMI connector on " + device);
    connector_id_ = conn->connector_id;
    take_connector_state(conn.get());

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
  destroy_buffers();
  if (fd_ >= 0) {
    drmDropMaster(fd_);
    ::close(fd_);
  }
}

void DrmOutput::take_connector_state(drmModeConnector* c) {
  connected_ = c->connection == DRM_MODE_CONNECTED;
  modes_.clear();
  edid_.clear();
  if (!connected_) return;
  modes_.assign(c->modes, c->modes + c->count_modes);
  read_edid();
}

bool DrmOutput::probe() {
  // drmModeGetConnector (not ...Current) re-runs detection and re-reads the EDID.
  ConnPtr c(drmModeGetConnector(fd_, connector_id_));
  if (!c) fail("drmModeGetConnector");
  take_connector_state(c.get());
  return connected_;
}

void DrmOutput::blank() {
  if (bufs_.empty()) return;
  // Detach the CRTC before freeing the framebuffers it scans out. A flip completion
  // still in flight is ignored by on_flip() because pending_ is reset.
  drmModeSetCrtc(fd_, crtc_id_, 0, 0, 0, nullptr, 0, nullptr);
  destroy_buffers();
}

void DrmOutput::destroy_buffers() {
  for (auto& b : bufs_) {
    if (b.prime_fd >= 0) ::close(b.prime_fd);
    if (b.map) munmap(b.map, b.size);
    if (b.fb_id) drmModeRmFB(fd_, b.fb_id);
    if (b.handle) {
      drm_mode_destroy_dumb d{};
      d.handle = b.handle;
      drmIoctl(fd_, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
  }
  bufs_.clear();
  front_ = pending_ = next_ = -1;
}

void DrmOutput::set_mode(const drmModeModeInfo& mode) {
  if (!bufs_.empty()) blank();
  mode_ = mode;
  bufs_.reserve(kBuffers);
  try {
    for (int i = 0; i < kBuffers; ++i) {
      Buffer& b = bufs_.emplace_back();
      drm_mode_create_dumb create{};
      create.width = mode_.hdisplay;
      create.height = mode_.vdisplay;
      create.bpp = 32;
      if (drmIoctl(fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) fail("create dumb buffer");
      b.handle = create.handle;
      b.pitch = create.pitch;
      b.size = create.size;

      const uint32_t handles[4] = {b.handle}, pitches[4] = {b.pitch}, offsets[4] = {0};
      if (drmModeAddFB2(fd_, mode_.hdisplay, mode_.vdisplay, DRM_FORMAT_XRGB8888, handles,
                        pitches, offsets, &b.fb_id, 0) != 0)
        fail("drmModeAddFB2");

      drm_mode_map_dumb map{};
      map.handle = b.handle;
      if (drmIoctl(fd_, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) fail("map dumb buffer");
      void* m = mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, map.offset);
      if (m == MAP_FAILED) fail("mmap dumb buffer");
      b.map = m;
      std::memset(b.map, 0, b.size);

      if (drmPrimeHandleToFD(fd_, b.handle, DRM_CLOEXEC | DRM_RDWR, &b.prime_fd) != 0)
        fail("export scanout buffer as DMA-BUF");
    }
    if (drmModeSetCrtc(fd_, crtc_id_, bufs_[0].fb_id, 0, 0, &connector_id_, 1, &mode_) != 0)
      fail("drmModeSetCrtc " + describe(mode_));
  } catch (...) {
    destroy_buffers();
    throw;
  }
  front_ = 0;
}

int DrmOutput::acquire() const {
  if (bufs_.empty()) throw std::logic_error("acquire() with no mode set");
  // A frame rendered but not yet submitted can simply be overwritten: newest wins.
  if (next_ >= 0) return next_;
  for (int i = 0; i < static_cast<int>(bufs_.size()); ++i)
    if (i != front_ && i != pending_) return i;
  throw std::logic_error("no free scanout buffer");  // impossible with kBuffers >= 3
}

void DrmOutput::present(int i) {
  if (pending_ >= 0) {
    next_ = i;
    return;
  }
  submit_flip(i);
}

void DrmOutput::submit_flip(int i) {
  if (drmModePageFlip(fd_, crtc_id_, bufs_.at(i).fb_id, DRM_MODE_PAGE_FLIP_EVENT, this) != 0) {
    // EBUSY: a flip we don't know about is still in flight; retry after the next event.
    if (errno == EBUSY) {
      next_ = i;
      return;
    }
    fail("drmModePageFlip");
  }
  pending_ = i;
}

void DrmOutput::on_flip(int, unsigned, unsigned, unsigned, void* self) {
  auto* o = static_cast<DrmOutput*>(self);
  if (o->pending_ < 0) return;  // stale event from before blank()/set_mode()
  o->front_ = o->pending_;
  o->pending_ = -1;
  if (o->next_ >= 0) {
    const int n = o->next_;
    o->next_ = -1;
    o->submit_flip(n);
  }
}

void DrmOutput::handle_events() {
  drmEventContext ctx{};
  ctx.version = 2;
  ctx.page_flip_handler = &DrmOutput::on_flip;
  drmHandleEvent(fd_, &ctx);
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

Surface DrmOutput::surface(int i) {
  const Buffer& b = bufs_.at(i);
  return Surface{static_cast<uint32_t*>(b.map), mode_.hdisplay, mode_.vdisplay, b.pitch / 4};
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
