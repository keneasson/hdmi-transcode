// hdmi-transcode-pattern: drive the HDMI output and show the test pattern (R4).
//
//   hdmi-transcode-pattern                 # automatic choice (R2/R2a)
//   hdmi-transcode-pattern --mode 1280x800@60
//   hdmi-transcode-pattern --list          # display's modes + the automatic choice
//
// Runs until SIGINT/SIGTERM (or --seconds N), then restores the previous screen.
// Needs DRM master: stop any display manager first.
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <optional>
#include <string>

#include "drm_output.h"
#include "edid.h"
#include "mode_select.h"
#include "test_pattern.h"

namespace {

int usage(const char* argv0) {
  std::fprintf(stderr, "usage: %s [--list] [--mode WxH[@Hz]] [--seconds N]\n", argv0);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);  // logs arrive promptly under systemd/redirects
  std::optional<ht::ModeRequest> req;
  bool list = false;
  long seconds = 0;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--list") {
      list = true;
    } else if (a == "--mode" && i + 1 < argc) {
      auto m = ht::parse_mode(argv[++i]);
      if (!m) return usage(argv[0]);
      req = m;
    } else if (a == "--seconds" && i + 1 < argc) {
      char* end = nullptr;
      seconds = std::strtol(argv[++i], &end, 10);
      if (*end || seconds < 0) return usage(argv[0]);
    } else {
      return usage(argv[0]);
    }
  }

  // Handle termination synchronously so the DrmOutput destructor always runs.
  sigset_t sigs;
  sigemptyset(&sigs);
  sigaddset(&sigs, SIGINT);
  sigaddset(&sigs, SIGTERM);
  sigaddset(&sigs, SIGHUP);
  pthread_sigmask(SIG_BLOCK, &sigs, nullptr);

  try {
    ht::DrmOutput out;
    const auto edid = ht::edid_preferred_timing(out.edid());
    const ht::ModeChoice choice = ht::choose_mode(out.modes(), edid, req);
    if (list) {
      std::printf("EDID: %s\n", out.edid().empty() ? "none"
                                 : edid ? "valid, preferred timing below" : "unusable");
      if (edid)
        std::printf("  EDID preferred: %dx%d @ %.2fHz\n", edid->width, edid->height,
                    edid->refresh_mhz() / 1000.0);
      for (const auto& m : out.modes())
        std::printf("%s%s\n", ht::describe(m).c_str(),
                    (m.type & DRM_MODE_TYPE_PREFERRED) ? "  (kernel preferred)" : "");
      std::printf("choice: %s (%s)\n", ht::describe(choice.mode).c_str(),
                  choice.reason.c_str());
      return 0;
    }
    if (req && choice.reason.rfind("requested", 0) != 0)
      std::fprintf(stderr, "requested mode not available; using the automatic choice\n");
    out.set_mode(choice.mode);
    out.keep_awake();
    ht::Surface s = out.surface();
    ht::draw_test_pattern(s, ht::describe(choice.mode));
    std::printf("showing test pattern at %s (%s)\n", ht::describe(choice.mode).c_str(),
                choice.reason.c_str());

    if (seconds > 0) {
      timespec ts{seconds, 0};
      sigtimedwait(&sigs, nullptr, &ts);
    } else {
      int sig = 0;
      sigwait(&sigs, &sig);
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}
