// Tests for the R3 aspect geometry, checked against the numbers in docs/requirements.md.
#include "aspect.h"
#include "check.h"

namespace {

using ht::AspectMode;
using ht::Rect;

bool even(const Rect& r) { return !(r.x & 1) && !(r.y & 1) && !(r.w & 1) && !(r.h & 1); }

void hd_to_1280x800() {
  // R3: compress = 11 % vertical stretch, letterbox = 40 px bars, crop = 5 % each side.
  auto c = ht::place(1920, 1080, 1280, 800, AspectMode::Compress);
  CHECK((c.src == Rect{0, 0, 1920, 1080}));
  CHECK((c.dst == Rect{0, 0, 1280, 800}));

  auto l = ht::place(1920, 1080, 1280, 800, AspectMode::Letterbox);
  CHECK((l.src == Rect{0, 0, 1920, 1080}));
  CHECK((l.dst == Rect{0, 40, 1280, 720}));

  auto k = ht::place(1920, 1080, 1280, 800, AspectMode::Crop);
  CHECK((k.src == Rect{96, 0, 1728, 1080}));
  CHECK((k.dst == Rect{0, 0, 1280, 800}));
}

void hd_to_1024x768() {
  auto l = ht::place(1920, 1080, 1024, 768, AspectMode::Letterbox);
  CHECK((l.dst == Rect{0, 96, 1024, 576}));
  auto k = ht::place(1920, 1080, 1024, 768, AspectMode::Crop);
  CHECK((k.src == Rect{240, 0, 1440, 1080}));
}

void same_aspect_is_pass_through() {
  for (auto m : {AspectMode::Compress, AspectMode::Letterbox, AspectMode::Crop}) {
    auto p = ht::place(1920, 1080, 1280, 720, m);
    CHECK((p.src == Rect{0, 0, 1920, 1080}));
    CHECK((p.dst == Rect{0, 0, 1280, 720}));
  }
}

void taller_source() {
  // 4:3 source on a 16:9 screen: pillarbox / crop top and bottom.
  auto l = ht::place(1024, 768, 1920, 1080, AspectMode::Letterbox);
  CHECK((l.dst == Rect{240, 0, 1440, 1080}));
  auto k = ht::place(1024, 768, 1920, 1080, AspectMode::Crop);
  CHECK((k.src == Rect{0, 96, 1024, 576}));
}

void everything_even_and_inside() {
  const int sizes[][2] = {{1920, 1080}, {1280, 720}, {720, 576}, {720, 480}, {1366, 768},
                          {1281, 721}, {640, 480}};
  const int screens[][2] = {{1280, 800}, {1024, 768}, {640, 480}, {1920, 1200}, {1279, 799}};
  for (auto& s : sizes)
    for (auto& d : screens)
      for (auto m : {AspectMode::Compress, AspectMode::Letterbox, AspectMode::Crop,
                     AspectMode::Native}) {
        auto p = ht::place(s[0], s[1], d[0], d[1], m);
        CHECK(even(p.src) && even(p.dst));
        CHECK(p.src.w > 0 && p.src.h > 0 && p.dst.w > 0 && p.dst.h > 0);
        CHECK(p.src.x + p.src.w <= s[0] && p.src.y + p.src.h <= s[1]);
        CHECK(p.dst.x + p.dst.w <= d[0] && p.dst.y + p.dst.h <= d[1]);
      }
}

void degenerate_inputs() {
  auto p = ht::place(0, 1080, 1280, 800, AspectMode::Letterbox);
  CHECK((p.src == Rect{}) && (p.dst == Rect{}));
  p = ht::place(1920, 1080, 1280, -1, AspectMode::Crop);
  CHECK((p.dst == Rect{}));
}

void parsing() {
  CHECK(ht::parse_aspect("compress") == AspectMode::Compress);
  CHECK(ht::parse_aspect("letterbox") == AspectMode::Letterbox);
  CHECK(ht::parse_aspect("crop") == AspectMode::Crop);
  CHECK(ht::parse_aspect("native") == AspectMode::Native);
  CHECK(!ht::parse_aspect("Compress"));
  CHECK(!ht::parse_aspect(""));
  for (auto m : {AspectMode::Compress, AspectMode::Letterbox, AspectMode::Crop,
                 AspectMode::Native})
    CHECK(ht::parse_aspect(ht::to_string(m)) == m);
}

}  // namespace

int main() {
  hd_to_1280x800();
  hd_to_1024x768();
  same_aspect_is_pass_through();
  taller_source();
  everything_even_and_inside();
  degenerate_inputs();
  parsing();
  return finish();
}
