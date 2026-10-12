// Where the input picture goes on the output (requirement R3). Pure geometry, no
// hardware: given the source and screen sizes and the aspect mode, which source
// rectangle is scaled into which screen rectangle. Everything outside the destination
// rectangle is black.
#pragma once

#include <optional>
#include <string>

namespace ht {

enum class AspectMode {
  Compress,   // stretch the whole frame over the whole screen (default)
  Letterbox,  // keep aspect, fit inside, black bars
  Crop,       // keep aspect, fill the screen, trim the source edges
  Native,     // R3: "plain scale-to-fit with no aspect correction"; geometrically the
              // same as Compress until the requirement says otherwise
};

std::optional<AspectMode> parse_aspect(const std::string& s);
const char* to_string(AspectMode m);

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool operator==(const Rect& o) const {
    return x == o.x && y == o.y && w == o.w && h == o.h;
  }
};

struct Placement {
  Rect src;  // part of the input frame used
  Rect dst;  // where it lands on the screen
};

// All coordinates and sizes come out even, as the RGA requires for 4:2:0/4:2:2 YUV.
// Sizes must be positive; a degenerate input yields an empty placement (all zero).
Placement place(int src_w, int src_h, int dst_w, int dst_h, AspectMode mode);

}  // namespace ht
