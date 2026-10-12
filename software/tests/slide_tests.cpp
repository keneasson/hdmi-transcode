// Tests for the slide (R13): PNG decoding, fitting onto the output, and the store.
#include <string>
#include <vector>

#include <png.h>
#include <unistd.h>

#include "check.h"
#include "slide.h"

namespace {

// Encodes `w`x`h` RGBA pixels (r,g,b,a bytes) as a PNG.
std::string encode_png(int w, int h, const std::vector<uint8_t>& rgba) {
  png_image pi{};
  pi.version = PNG_IMAGE_VERSION;
  pi.width = static_cast<png_uint_32>(w);
  pi.height = static_cast<png_uint_32>(h);
  pi.format = PNG_FORMAT_RGBA;
  png_alloc_size_t size = 0;
  if (!png_image_write_to_memory(&pi, nullptr, &size, 0, rgba.data(), 0, nullptr)) return {};
  std::string out(size, '\0');
  if (!png_image_write_to_memory(&pi, out.data(), &size, 0, rgba.data(), 0, nullptr)) return {};
  out.resize(size);
  return out;
}

std::string solid_png(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
  std::vector<uint8_t> px;
  for (int i = 0; i < w * h; ++i) px.insert(px.end(), {r, g, b, a});
  return encode_png(w, h, px);
}

void decoding() {
  std::string err;
  auto img = ht::decode_png(solid_png(3, 2, 0x12, 0x34, 0x56), err);
  CHECK(img && img->width == 3 && img->height == 2 && img->pixels.size() == 6);
  CHECK(img && img->pixels[5] == ht::rgb(0x12, 0x34, 0x56));

  img = ht::decode_png(solid_png(1, 1, 200, 100, 50, 0), err);  // fully transparent
  CHECK(img && img->pixels[0] == ht::rgb(0, 0, 0));
  img = ht::decode_png(solid_png(1, 1, 255, 255, 255, 128), err);  // half over black
  CHECK(img && (img->pixels[0] & 0xff) >= 127 && (img->pixels[0] & 0xff) <= 128);

  err.clear();
  CHECK(!ht::decode_png("not a png at all", err) && !err.empty());
  err.clear();
  CHECK(!ht::decode_png(solid_png(ht::kMaxSlideSide + 1, 1, 0, 0, 0), err) && !err.empty());
  const std::string good = solid_png(4, 4, 1, 2, 3);
  err.clear();
  CHECK(!ht::decode_png(good.substr(0, good.size() / 2), err) && !err.empty());  // truncated
}

struct Canvas {
  std::vector<uint32_t> px;
  ht::Surface s;
  Canvas(int w, int h, size_t stride)
      : px(stride * h, 0x12345678u), s{px.data(), w, h, stride} {}
};

void fitting() {
  std::string err;
  // Same size: exact copy, stride respected.
  auto img = ht::decode_png(solid_png(8, 4, 10, 20, 30), err);
  Canvas c(8, 4, 10);
  ht::draw_image_fit(c.s, *img);
  CHECK(c.s.at(0, 0) == ht::rgb(10, 20, 30) && c.s.at(7, 3) == ht::rgb(10, 20, 30));
  CHECK(c.px[8] == 0x12345678u);  // padding beyond the width untouched

  // 16:9 picture on a 16:10 screen: bars top and bottom, colour kept by the scaler.
  img = ht::decode_png(solid_png(160, 90, 200, 100, 50), err);
  Canvas d(320, 200, 320);
  ht::draw_image_fit(d.s, *img);
  CHECK(d.s.at(160, 0) == ht::rgb(0, 0, 0) && d.s.at(160, 199) == ht::rgb(0, 0, 0));
  CHECK(d.s.at(160, 100) == ht::rgb(200, 100, 50));
  CHECK(d.s.at(0, 10) == ht::rgb(200, 100, 50) && d.s.at(319, 189) == ht::rgb(200, 100, 50));
  CHECK(d.s.at(0, 9) == ht::rgb(0, 0, 0) && d.s.at(0, 190) == ht::rgb(0, 0, 0));

  // 4:3 picture on 16:10: bars left and right; downscale.
  img = ht::decode_png(solid_png(400, 300, 1, 2, 3), err);
  ht::draw_image_fit(d.s, *img);
  CHECK(d.s.at(0, 100) == ht::rgb(0, 0, 0) && d.s.at(160, 100) == ht::rgb(1, 2, 3));
  CHECK(d.s.at(26, 100) == ht::rgb(0, 0, 0) && d.s.at(27, 100) == ht::rgb(1, 2, 3));
}

void store() {
  char dir[] = "/tmp/ht-slide-XXXXXX";
  CHECK(mkdtemp(dir) != nullptr);
  {
    ht::SlideStore s(dir);
    CHECK(!s.has_slide() && s.spec().empty());
    const unsigned v0 = s.version();
    CHECK(s.save(solid_png(16, 10, 9, 9, 9), R"({"title":"Next"})").empty());
    CHECK(s.has_slide() && s.image().width == 16 && s.version() != v0);
    CHECK(!s.save("junk", "{}").empty());  // rejected: old slide kept
    CHECK(s.has_slide() && s.spec() == R"({"title":"Next"})");
    CHECK(!s.save(solid_png(2, 2, 0, 0, 0), std::string(ht::kMaxSlideSpecBytes + 1, 'x')).empty());
  }
  {
    ht::SlideStore s(dir);  // survives a restart (R5)
    CHECK(s.has_slide() && s.image().height == 10 && s.spec() == R"({"title":"Next"})");
    s.clear();
    CHECK(!s.has_slide());
  }
  ht::SlideStore s(dir);
  CHECK(!s.has_slide());
  rmdir(dir);
}

}  // namespace

int main() {
  decoding();
  fitting();
  store();
  return finish();
}
