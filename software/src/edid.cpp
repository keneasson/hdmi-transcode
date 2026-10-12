#include "edid.h"

namespace ht {

int EdidTiming::refresh_mhz() const {
  if (htotal <= 0 || vtotal <= 0) return 0;
  const double hz = pixel_clock_khz * 1000.0 / (double(htotal) * vtotal);
  return static_cast<int>(hz * 1000.0 + 0.5);  // field rate for interlaced timings
}

namespace {

// Header and base-block checksum.
bool valid_base_block(const std::vector<uint8_t>& e) {
  static constexpr uint8_t kHeader[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
  if (e.size() < 128) return false;
  for (int i = 0; i < 8; ++i)
    if (e[i] != kHeader[i]) return false;
  uint8_t sum = 0;
  for (int i = 0; i < 128; ++i) sum = static_cast<uint8_t>(sum + e[i]);
  return sum == 0;
}

}  // namespace

std::optional<EdidTiming> edid_preferred_timing(const std::vector<uint8_t>& e) {
  if (!valid_base_block(e)) return std::nullopt;

  // Four 18-byte descriptors at 54, 72, 90, 108; a detailed timing has a non-zero
  // pixel clock. Only the first one is "preferred", but skip non-timing descriptors
  // in case a display puts its name first.
  for (int off = 54; off <= 108; off += 18) {
    const uint8_t* d = &e[off];
    const int clock_10khz = d[0] | (d[1] << 8);
    if (clock_10khz == 0) continue;
    EdidTiming t;
    t.pixel_clock_khz = clock_10khz * 10;
    t.width = d[2] | ((d[4] & 0xf0) << 4);
    const int hblank = d[3] | ((d[4] & 0x0f) << 8);
    t.height = d[5] | ((d[7] & 0xf0) << 4);
    const int vblank = d[6] | ((d[7] & 0x0f) << 8);
    t.interlaced = (d[17] & 0x80) != 0;
    t.htotal = t.width + hblank;
    t.vtotal = t.height + vblank;
    if (t.width == 0 || t.height == 0) return std::nullopt;
    return t;
  }
  return std::nullopt;
}

std::string edid_display_name(const std::vector<uint8_t>& e) {
  if (!valid_base_block(e)) return {};
  // Display product name descriptor: 00 00 00 FC 00, then up to 13 ASCII characters
  // ended by 0x0A and padded with spaces.
  for (int off = 54; off <= 108; off += 18) {
    const uint8_t* d = &e[off];
    if (d[0] || d[1] || d[2] || d[3] != 0xfc) continue;
    std::string name;
    for (int i = 5; i < 18 && d[i] != 0x0a; ++i)
      name += (d[i] >= 0x20 && d[i] < 0x7f) ? static_cast<char>(d[i]) : '?';
    while (!name.empty() && name.back() == ' ') name.pop_back();
    return name;
  }
  return {};
}

}  // namespace ht
