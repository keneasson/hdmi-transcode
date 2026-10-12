// Hardware scale + colour convert with the Rockchip RGA (requirement N2: no CPU pixel
// processing). One call takes a captured YUV frame (DMA-BUF), scales the chosen source
// rectangle into the destination rectangle of an XRGB8888 scanout buffer (DMA-BUF), and
// paints everything else in that buffer black.
#pragma once

#include <string>

#include "aspect.h"

namespace ht {

struct DmaImage {
  int fd = -1;
  int width = 0, height = 0;
  int stride_px = 0;   // row pitch in pixels (luma pixels for YUV)
  int height_px = 0;   // rows allocated per plane (>= height)
  int rk_format = 0;   // RK_FORMAT_* from <rga/rga.h>
};

enum class YuvMatrix { Bt601Limited, Bt709Limited, Bt601Full };

class RgaScaler {
 public:
  // Throws std::runtime_error with RGA's own message if the hardware rejects the job.
  void blit(const DmaImage& src, const DmaImage& dst, const Placement& p, YuvMatrix matrix);
};

}  // namespace ht
