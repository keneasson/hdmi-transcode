#include "rga_scaler.h"

#include <stdexcept>
#include <vector>

#include <rga/im2d.h>

namespace ht {
namespace {

IM_COLOR_SPACE_MODE matrix_mode(YuvMatrix m) {
  switch (m) {
    case YuvMatrix::Bt601Limited: return IM_YUV_TO_RGB_BT601_LIMIT;
    case YuvMatrix::Bt709Limited: return IM_YUV_TO_RGB_BT709_LIMIT;
    case YuvMatrix::Bt601Full: return IM_YUV_TO_RGB_BT601_FULL;
  }
  return IM_YUV_TO_RGB_BT709_LIMIT;
}

rga_buffer_t wrap(const DmaImage& i) {
  return wrapbuffer_fd_t(i.fd, i.width, i.height, i.stride_px, i.height_px, i.rk_format);
}

im_rect to_im(const Rect& r) { return im_rect{r.x, r.y, r.w, r.h}; }

}  // namespace

void RgaScaler::blit(const DmaImage& src, const DmaImage& dst, const Placement& p,
                     YuvMatrix matrix) {
  rga_buffer_t s = wrap(src), d = wrap(dst);
  // The colour matrix goes on the source buffer. Passing it in improcess()'s usage word
  // does not work: there its low bits mean rotate/flip, and librga 1.10 logs an error
  // per frame yet still returns success (seen 2026-10-11).
  imsetColorSpace(&s, matrix_mode(matrix));

  // Black bars: whatever part of the screen the picture does not cover.
  std::vector<Rect> bars;
  if (p.dst.y > 0) bars.push_back({0, 0, dst.width, p.dst.y});
  if (p.dst.y + p.dst.h < dst.height)
    bars.push_back({0, p.dst.y + p.dst.h, dst.width, dst.height - p.dst.y - p.dst.h});
  if (p.dst.x > 0) bars.push_back({0, p.dst.y, p.dst.x, p.dst.h});
  if (p.dst.x + p.dst.w < dst.width)
    bars.push_back({p.dst.x + p.dst.w, p.dst.y, dst.width - p.dst.x - p.dst.w, p.dst.h});
  for (const Rect& b : bars) {
    const IM_STATUS st = imfill(d, to_im(b), 0xff000000);
    if (st != IM_STATUS_SUCCESS && st != IM_STATUS_NOERROR)
      throw std::runtime_error(std::string("RGA fill: ") + imStrError(st));
  }

  rga_buffer_t pat{};
  const IM_STATUS st = improcess(s, d, pat, to_im(p.src), to_im(p.dst), im_rect{}, -1,
                                 nullptr, nullptr, IM_SYNC);
  if (st != IM_STATUS_SUCCESS && st != IM_STATUS_NOERROR)
    throw std::runtime_error(std::string("RGA scale: ") + imStrError(st));
}

}  // namespace ht
