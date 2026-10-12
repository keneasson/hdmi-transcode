// Tests for the generated test pattern. No display needed:
// everything draws into a heap buffer.
#include <vector>

#include "check.h"
#include "test_pattern.h"

namespace {

struct Buffer {
  std::vector<uint32_t> px;
  ht::Surface s;
  Buffer(int w, int h, int pad = 0)
      : px(static_cast<size_t>(w + pad) * h, 0xdeadbeef),
        s{px.data(), w, h, static_cast<size_t>(w + pad)} {}
};

void border_is_white_at_every_target() {
  for (auto [w, h] : {std::pair{1280, 800}, {1024, 768}, {640, 480}, {1920, 1080}}) {
    Buffer b(w, h);
    ht::draw_test_pattern(b.s, "TEST");
    const uint32_t white = ht::rgb(255, 255, 255);
    CHECK(b.s.at(0, 0) == white);
    CHECK(b.s.at(w - 1, 0) == white);
    CHECK(b.s.at(0, h - 1) == white);
    CHECK(b.s.at(w - 1, h - 1) == white);
    CHECK(b.s.at(w / 2, h - 1) == white);
  }
}

void stride_padding_is_never_written() {
  Buffer b(1024, 768, 64);
  ht::draw_test_pattern(b.s, "1024x768 @ 60Hz");
  for (int y = 0; y < 768; ++y)
    for (int x = 1024; x < 1024 + 64; ++x) CHECK(b.px[static_cast<size_t>(y) * 1088 + x] == 0xdeadbeef);
}

void fill_rect_clips() {
  Buffer b(16, 8);
  ht::fill_rect(b.s, -5, -5, 100, 100, 1);
  for (uint32_t v : b.px) CHECK(v == 1);
  ht::fill_rect(b.s, 20, 20, 5, 5, 2);  // fully outside: no write, no crash
  for (uint32_t v : b.px) CHECK(v == 1);
}

void text_metrics() {
  CHECK(ht::text_width("", 3) == 0);
  CHECK(ht::text_width("A", 1) == 5);
  CHECK(ht::text_width("AB", 2) == 22);
  Buffer b(64, 16);
  ht::fill_rect(b.s, 0, 0, 64, 16, 0);
  ht::draw_text(b.s, 0, 0, 1, "1", 7);
  CHECK(b.s.at(2, 0) == 7);  // the stem of '1'
  CHECK(b.s.at(0, 0) == 0);
}

}  // namespace

int main() {
  border_is_white_at_every_target();
  stride_padding_is_never_written();
  fill_rect_clips();
  text_metrics();
  return finish();
}
