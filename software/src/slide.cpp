#include "slide.h"

#include <algorithm>
#include <cstdio>

#include <png.h>
#include <unistd.h>

#include "file_util.h"

namespace ht {

std::optional<Image> decode_png(const std::string& png, std::string& error) {
  if (png.size() > kMaxSlidePngBytes) {
    error = "The picture file is too large (4 MB at most).";
    return std::nullopt;
  }
  png_image pi{};
  pi.version = PNG_IMAGE_VERSION;
  if (!png_image_begin_read_from_memory(&pi, png.data(), png.size())) {
    error = "That is not a PNG picture the box can read.";
    return std::nullopt;
  }
  if (pi.width == 0 || pi.height == 0 || pi.width > kMaxSlideSide ||
      pi.height > kMaxSlideSide) {
    png_image_free(&pi);
    error = "The picture is too big (4096 pixels per side at most).";
    return std::nullopt;
  }
  // BGRA bytes in memory are one little-endian XRGB8888 word per pixel.
  pi.format = PNG_FORMAT_BGRA;
  Image img;
  img.width = static_cast<int>(pi.width);
  img.height = static_cast<int>(pi.height);
  img.pixels.resize(static_cast<size_t>(img.width) * img.height);
  png_color black{0, 0, 0};
  if (!png_image_finish_read(&pi, &black, img.pixels.data(), 0, nullptr)) {
    png_image_free(&pi);
    error = "The picture is damaged.";
    return std::nullopt;
  }
  // Composite alpha over black so what is stored is exactly what is shown.
  for (auto& p : img.pixels) {
    const uint32_t a = p >> 24;
    if (a == 255) continue;
    const auto ch = [&](int shift) { return ((p >> shift) & 0xff) * a / 255; };
    p = 0xff000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
  }
  return img;
}

void draw_image_fit(Surface& dst, const Image& img) {
  fill_rect(dst, 0, 0, dst.width, dst.height, rgb(0, 0, 0));
  if (img.width <= 0 || img.height <= 0 || dst.width <= 0 || dst.height <= 0) return;
  // Fit inside, keep shape (compare dst.w/img.w with dst.h/img.h in integers).
  int w = dst.width, h = dst.height;
  if (static_cast<int64_t>(dst.width) * img.height > static_cast<int64_t>(dst.height) * img.width)
    w = static_cast<int>(static_cast<int64_t>(dst.height) * img.width / img.height);
  else
    h = static_cast<int>(static_cast<int64_t>(dst.width) * img.height / img.width);
  const int x0 = (dst.width - w) / 2, y0 = (dst.height - h) / 2;

  if (w == img.width && h == img.height) {
    for (int y = 0; y < h; ++y)
      std::copy_n(&img.pixels[static_cast<size_t>(y) * img.width], w, &dst.at(x0, y0 + y));
    return;
  }
  // Bilinear, 16.16 fixed point, sampling at pixel centres.
  const int64_t sx = (static_cast<int64_t>(img.width) << 16) / w;
  const int64_t sy = (static_cast<int64_t>(img.height) << 16) / h;
  for (int y = 0; y < h; ++y) {
    const int64_t fy = std::max<int64_t>(0, (2 * y + 1) * sy / 2 - 0x8000);
    const int y1 = std::min(static_cast<int>(fy >> 16), img.height - 1);
    const int y2 = std::min(y1 + 1, img.height - 1);
    const uint32_t wy = static_cast<uint32_t>(fy & 0xffff) >> 8;  // 0..255
    const uint32_t* r1 = &img.pixels[static_cast<size_t>(y1) * img.width];
    const uint32_t* r2 = &img.pixels[static_cast<size_t>(y2) * img.width];
    for (int x = 0; x < w; ++x) {
      const int64_t fx = std::max<int64_t>(0, (2 * x + 1) * sx / 2 - 0x8000);
      const int x1 = std::min(static_cast<int>(fx >> 16), img.width - 1);
      const int x2 = std::min(x1 + 1, img.width - 1);
      const uint32_t wx = static_cast<uint32_t>(fx & 0xffff) >> 8;
      uint32_t out = 0xff000000u;
      for (int shift = 0; shift <= 16; shift += 8) {
        const auto c = [&](uint32_t p) { return (p >> shift) & 0xff; };
        const uint32_t top = c(r1[x1]) * (256 - wx) + c(r1[x2]) * wx;
        const uint32_t bot = c(r2[x1]) * (256 - wx) + c(r2[x2]) * wx;
        out |= ((top * (256 - wy) + bot * wy) >> 16) << shift;
      }
      dst.at(x0 + x, y0 + y) = out;
    }
  }
}

SlideStore::SlideStore(std::string dir) : dir_(std::move(dir)) {
  const auto png = read_file(png_path());
  if (!png) return;
  std::string err;
  image_ = decode_png(*png, err);
  if (!image_) {
    std::printf("slide %s ignored: %s\n", png_path().c_str(), err.c_str());
    return;
  }
  spec_ = read_file(spec_path()).value_or("");
  if (spec_.size() > kMaxSlideSpecBytes) spec_.clear();
}

std::string SlideStore::save(const std::string& png, const std::string& spec) {
  if (spec.size() > kMaxSlideSpecBytes) return "The slide text is too long.";
  std::string err;
  auto img = decode_png(png, err);
  if (!img) return err;
  // Picture first: a power cut between the two leaves the new picture with the old text,
  // which the page can simply save again.
  write_file_atomic(png_path(), png);
  write_file_atomic(spec_path(), spec);
  image_ = std::move(img);
  spec_ = spec;
  ++version_;
  return "";
}

void SlideStore::clear() {
  ::unlink(png_path().c_str());
  ::unlink(spec_path().c_str());
  image_.reset();
  spec_.clear();
  ++version_;
}

}  // namespace ht
