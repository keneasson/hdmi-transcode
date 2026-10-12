// Generated test pattern (requirement R4), drawn at the exact output resolution so the
// current mode is self-evident on the display.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ht {

// A CPU-writable XRGB8888 surface. Does not own the memory.
struct Surface {
  uint32_t* pixels;
  int width;
  int height;
  size_t stride_px;  // pixels per row (>= width)

  uint32_t& at(int x, int y) { return pixels[static_cast<size_t>(y) * stride_px + x]; }
};

constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return 0xff000000u | (uint32_t{r} << 16) | (uint32_t{g} << 8) | b;
}

// Draws the full pattern: 1 px border (overscan check), 16:9 and 4:3 safe-area frames,
// colour bars, grey ramp, a centre circle and corner circles (distortion check), and
// `label` (e.g. "1280x800 @ 60Hz") in large text.
void draw_test_pattern(Surface& s, const std::string& label);

// Exposed for tests.
void fill_rect(Surface& s, int x, int y, int w, int h, uint32_t c);
// Returns the drawn width in pixels; characters without a glyph are drawn as blanks.
int draw_text(Surface& s, int x, int y, int scale, const std::string& text, uint32_t c);
int text_width(const std::string& text, int scale);

}  // namespace ht
