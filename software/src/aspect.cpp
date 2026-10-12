#include "aspect.h"

#include <algorithm>
#include <cstdint>

namespace ht {
namespace {

int even_down(int v) { return v & ~1; }
// Nearest even value of num/den (num, den > 0).
int even_round(int64_t num, int64_t den) { return static_cast<int>(((num + den) / (2 * den)) * 2); }

}  // namespace

std::optional<AspectMode> parse_aspect(const std::string& s) {
  if (s == "compress") return AspectMode::Compress;
  if (s == "letterbox") return AspectMode::Letterbox;
  if (s == "crop") return AspectMode::Crop;
  if (s == "native") return AspectMode::Native;
  return std::nullopt;
}

const char* to_string(AspectMode m) {
  switch (m) {
    case AspectMode::Compress: return "compress";
    case AspectMode::Letterbox: return "letterbox";
    case AspectMode::Crop: return "crop";
    case AspectMode::Native: return "native";
  }
  return "?";
}

Placement place(int sw, int sh, int dw, int dh, AspectMode mode) {
  if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return {};
  sw = even_down(sw);
  sh = even_down(sh);
  dw = even_down(dw);
  dh = even_down(dh);
  const Rect full_src{0, 0, sw, sh}, full_dst{0, 0, dw, dh};
  // Compare aspect ratios exactly: sw/sh vs dw/dh  <=>  sw*dh vs dw*sh.
  const int64_t src_ar = int64_t{sw} * dh, dst_ar = int64_t{dw} * sh;

  switch (mode) {
    case AspectMode::Compress:
    case AspectMode::Native:
      return {full_src, full_dst};

    case AspectMode::Letterbox: {
      if (src_ar == dst_ar) return {full_src, full_dst};
      Rect d = full_dst;
      if (src_ar > dst_ar) {  // source wider: full width, bars top and bottom
        d.h = std::min(dh, even_round(int64_t{dw} * sh, sw));
        d.y = even_down((dh - d.h) / 2);
      } else {  // source taller: full height, bars left and right
        d.w = std::min(dw, even_round(int64_t{dh} * sw, sh));
        d.x = even_down((dw - d.w) / 2);
      }
      return {full_src, d};
    }

    case AspectMode::Crop: {
      if (src_ar == dst_ar) return {full_src, full_dst};
      Rect s = full_src;
      if (src_ar > dst_ar) {  // source wider: use the middle, trim left and right
        s.w = std::min(sw, even_round(int64_t{sh} * dw, dh));
        s.x = even_down((sw - s.w) / 2);
      } else {  // source taller: trim top and bottom
        s.h = std::min(sh, even_round(int64_t{sw} * dh, dw));
        s.y = even_down((sh - s.h) / 2);
      }
      return {s, full_dst};
    }
  }
  return {};
}

}  // namespace ht
