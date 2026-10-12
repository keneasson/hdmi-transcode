// The slide (requirement R13): a still picture shown instead of the test pattern, e.g.
// "Coming up next" between talks.
//
// The phone page lays the slide out (template, text, colours) and renders it to a PNG at
// the projector's resolution, so the box needs no fonts or layout code and an uploaded
// photo is the same thing to it. The box keeps two files: the PNG it shows, and the
// page's own description of the slide (opaque JSON) so the page can edit it again.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "test_pattern.h"  // Surface

namespace ht {

// Largest slide accepted, in pixels per side and in PNG bytes.
constexpr int kMaxSlideSide = 4096;
constexpr size_t kMaxSlidePngBytes = 4 * 1024 * 1024;
constexpr size_t kMaxSlideSpecBytes = 16 * 1024;

// Decoded XRGB8888 picture, tightly packed.
struct Image {
  int width = 0;
  int height = 0;
  std::vector<uint32_t> pixels;
};

// Decodes a PNG (any colour type; alpha is composited over black). On failure returns
// nullopt and says why in `error` (wording fit for the phone page).
std::optional<Image> decode_png(const std::string& png, std::string& error);

// Draws `img` onto `dst` as large as fits with its shape kept, centred, black around it.
// Same size is a straight copy; otherwise bilinear. CPU, so only for still pictures.
void draw_image_fit(Surface& dst, const Image& img);

class SlideStore {
 public:
  // Files live in `dir` (created on first save). Loads what is there; a damaged file is
  // treated as no slide.
  explicit SlideStore(std::string dir);

  bool has_slide() const { return image_.has_value(); }
  const Image& image() const { return *image_; }
  const std::string& spec() const { return spec_; }  // "" when none
  // Bumped on every change so the display knows to redraw.
  unsigned version() const { return version_; }

  // Validates and stores a new slide; the old one stays if anything is wrong. Returns ""
  // on success, else a reason fit for the phone page. Throws std::runtime_error if the
  // files cannot be written.
  std::string save(const std::string& png, const std::string& spec);
  void clear();

 private:
  std::string png_path() const { return dir_ + "/slide.png"; }
  std::string spec_path() const { return dir_ + "/slide.json"; }

  std::string dir_;
  std::optional<Image> image_;
  std::string spec_;
  unsigned version_ = 0;
};

}  // namespace ht
