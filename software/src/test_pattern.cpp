#include "test_pattern.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ht {
namespace {

// Classic 5x7 font, column-major, bit 0 = top row. Only the characters the pattern
// labels need; anything else renders as a blank cell.
struct Glyph {
  char ch;
  std::array<uint8_t, 5> cols;
};

constexpr Glyph kFont[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}}, {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}}, {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}}, {'@', {0x32, 0x49, 0x79, 0x41, 0x3E}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}}, {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}}, {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}}, {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}}, {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}}, {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}}, {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}}, {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}}, {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}}, {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}}, {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}}, {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}}, {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}}, {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}}, {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}}, {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}}, {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}}, {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}}, {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
    {'x', {0x44, 0x28, 0x10, 0x28, 0x44}}, {'z', {0x44, 0x64, 0x54, 0x4C, 0x44}},
};

const Glyph* find_glyph(char c) {
  for (const auto& g : kFont)
    if (g.ch == c) return &g;
  return nullptr;
}

constexpr int kGlyphW = 5, kGlyphH = 7, kAdvance = 6;

// Outline of a w:h rectangle, the largest of that aspect that fits, centred.
void draw_aspect_frame(Surface& s, int aw, int ah, int t, uint32_t c) {
  int w = s.width, h = s.width * ah / aw;
  if (h > s.height) {
    h = s.height;
    w = s.height * aw / ah;
  }
  const int x = (s.width - w) / 2, y = (s.height - h) / 2;
  fill_rect(s, x, y, w, t, c);
  fill_rect(s, x, y + h - t, w, t, c);
  fill_rect(s, x, y, t, h, c);
  fill_rect(s, x + w - t, y, t, h, c);
}

// Ring of outer radius r and thickness t, drawn as horizontal spans (clipped).
void draw_ring(Surface& s, int cx, int cy, int r, int t, uint32_t c) {
  const int ri = std::max(0, r - t);
  for (int dy = -r; dy <= r; ++dy) {
    const int y = cy + dy;
    if (y < 0 || y >= s.height) continue;
    const int xo = static_cast<int>(std::sqrt(double(r) * r - double(dy) * dy));
    if (std::abs(dy) >= ri) {
      fill_rect(s, cx - xo, y, 2 * xo + 1, 1, c);
      continue;
    }
    const int xi = static_cast<int>(std::sqrt(double(ri) * ri - double(dy) * dy));
    fill_rect(s, cx - xo, y, xo - xi, 1, c);
    fill_rect(s, cx + xi + 1, y, xo - xi, 1, c);
  }
}

}  // namespace

void fill_rect(Surface& s, int x, int y, int w, int h, uint32_t c) {
  const int x0 = std::max(0, x), y0 = std::max(0, y);
  const int x1 = std::min(s.width, x + w), y1 = std::min(s.height, y + h);
  for (int yy = y0; yy < y1; ++yy)
    std::fill(&s.at(x0, yy), &s.at(x0, yy) + std::max(0, x1 - x0), c);
}

int text_width(const std::string& text, int scale) {
  return text.empty() ? 0 : (static_cast<int>(text.size()) * kAdvance - 1) * scale;
}

int draw_text(Surface& s, int x, int y, int scale, const std::string& text, uint32_t c) {
  int pen = x;
  for (char ch : text) {
    if (const Glyph* g = find_glyph(ch)) {
      for (int col = 0; col < kGlyphW; ++col)
        for (int row = 0; row < kGlyphH; ++row)
          if (g->cols[col] & (1u << row))
            fill_rect(s, pen + col * scale, y + row * scale, scale, scale, c);
    }
    pen += kAdvance * scale;
  }
  return text_width(text, scale);
}

void draw_test_pattern(Surface& s, const std::string& label) {
  const int W = s.width, H = s.height;
  const int line = std::max(2, H / 270);
  fill_rect(s, 0, 0, W, H, rgb(0, 0, 0));

  // Colour bars: white, yellow, cyan, green, magenta, red, blue, black.
  static constexpr uint32_t kBars[] = {rgb(255, 255, 255), rgb(255, 255, 0), rgb(0, 255, 255),
                                       rgb(0, 255, 0),     rgb(255, 0, 255), rgb(255, 0, 0),
                                       rgb(0, 0, 255),     rgb(0, 0, 0)};
  const int bars_y = H * 22 / 100, bars_h = H * 20 / 100;
  for (int i = 0; i < 8; ++i) {
    const int x0 = W * i / 8, x1 = W * (i + 1) / 8;
    fill_rect(s, x0, bars_y, x1 - x0, bars_h, kBars[i]);
  }

  // Continuous grey ramp, then an 11-step ramp under it.
  const int ramp_y = H * 68 / 100, ramp_h = H * 7 / 100;
  for (int x = 0; x < W; ++x) {
    const auto v = static_cast<uint8_t>(W > 1 ? x * 255 / (W - 1) : 0);
    fill_rect(s, x, ramp_y, 1, ramp_h, rgb(v, v, v));
  }
  for (int i = 0; i < 11; ++i) {
    const auto v = static_cast<uint8_t>(i * 255 / 10);
    const int x0 = W * i / 11, x1 = W * (i + 1) / 11;
    fill_rect(s, x0, ramp_y + ramp_h, x1 - x0, ramp_h, rgb(v, v, v));
  }

  // Safe areas, then circles that must look round on a correctly driven display.
  draw_aspect_frame(s, 16, 9, line, rgb(0, 200, 0));
  draw_aspect_frame(s, 4, 3, line, rgb(255, 140, 0));
  draw_ring(s, W / 2, H / 2, H * 45 / 100, line, rgb(255, 255, 255));
  const int cr = H / 10, inset = cr + H / 40;
  for (int cx : {inset, W - 1 - inset})
    for (int cy : {inset, H - 1 - inset}) draw_ring(s, cx, cy, cr, line, rgb(255, 255, 255));

  // Label on a black backing so it stays legible over the pattern.
  const int scale = std::max(2, H / 160);
  const int tw = text_width(label, scale), th = kGlyphH * scale;
  const int tx = (W - tw) / 2, ty = H * 8 / 100;
  fill_rect(s, tx - 2 * scale, ty - 2 * scale, tw + 4 * scale, th + 4 * scale, rgb(0, 0, 0));
  draw_text(s, tx, ty, scale, label, rgb(255, 255, 255));

  // 1 px border last: if any edge is missing, the display is overscanning.
  fill_rect(s, 0, 0, W, 1, rgb(255, 255, 255));
  fill_rect(s, 0, H - 1, W, 1, rgb(255, 255, 255));
  fill_rect(s, 0, 0, 1, H, rgb(255, 255, 255));
  fill_rect(s, W - 1, 0, 1, H, rgb(255, 255, 255));
}

}  // namespace ht
