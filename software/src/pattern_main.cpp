// hdmi-transcode-pattern: drive the HDMI output and show the test pattern (R4).
//
//   hdmi-transcode-pattern                 # automatic choice (R2/R2a)
//   hdmi-transcode-pattern --mode 1280x800@60
//   hdmi-transcode-pattern --list          # display's modes + the automatic choice
//
// Runs until SIGINT/SIGTERM (or --seconds N), then restores the previous screen. A display
// unplugged and plugged back (or swapped for another) is re-detected and re-driven without
// a restart (R2b). Needs DRM master: stop any display manager first.
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>

#include <poll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "drm_output.h"
#include "edid.h"
#include "hotplug.h"
#include "mode_select.h"
#include "test_pattern.h"

namespace {

int usage(const char* argv0) {
  std::fprintf(stderr, "usage: %s [--list] [--mode WxH[@Hz]] [--seconds N]\n", argv0);
  return 2;
}

void print_list(const ht::DrmOutput& out, const std::optional<ht::ModeRequest>& req) {
  if (!out.connected()) {
    std::printf("no display connected\n");
    return;
  }
  const auto edid = ht::edid_preferred_timing(out.edid());
  std::printf("EDID: %s\n", out.edid().empty() ? "none"
                            : edid             ? "valid, preferred timing below"
                                               : "unusable");
  if (edid)
    std::printf("  EDID preferred: %dx%d @ %.2fHz\n", edid->width, edid->height,
                edid->refresh_mhz() / 1000.0);
  for (const auto& m : out.modes())
    std::printf("%s%s\n", ht::describe(m).c_str(),
                (m.type & DRM_MODE_TYPE_PREFERRED) ? "  (kernel preferred)" : "");
  const auto choice = ht::choose_mode(out.modes(), edid, req);
  std::printf("choice: %s (%s)\n", ht::describe(choice.mode).c_str(), choice.reason.c_str());
}

// Brings the output in line with what is (or isn't) plugged in. Re-mode-sets only when
// the chosen timing changes, so an unchanged display is never disturbed.
void update(ht::DrmOutput& out, const std::optional<ht::ModeRequest>& req) {
  if (!out.connected()) {
    if (out.active()) {
      out.blank();
      std::printf("display disconnected; output off until one is connected\n");
    }
    return;
  }
  const ht::ModeChoice choice =
      ht::choose_mode(out.modes(), ht::edid_preferred_timing(out.edid()), req);
  if (out.active() && ht::same_timing(out.mode(), choice.mode)) return;
  if (req && choice.reason.rfind("requested", 0) != 0)
    std::fprintf(stderr, "requested mode not available; using the automatic choice\n");
  out.set_mode(choice.mode);
  out.keep_awake();
  ht::Surface s = out.surface();
  ht::draw_test_pattern(s, ht::describe(choice.mode));
  std::printf("showing test pattern at %s (%s)\n", ht::describe(choice.mode).c_str(),
              choice.reason.c_str());
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

  // Signals arrive through a signalfd so the DrmOutput destructor always runs.
  sigset_t sigs;
  sigemptyset(&sigs);
  sigaddset(&sigs, SIGINT);
  sigaddset(&sigs, SIGTERM);
  sigaddset(&sigs, SIGHUP);
  pthread_sigmask(SIG_BLOCK, &sigs, nullptr);
  const int sfd = signalfd(-1, &sigs, SFD_CLOEXEC);
  if (sfd < 0) {
    std::perror("signalfd");
    return 1;
  }

  try {
    ht::DrmOutput out;
    if (list) {
      print_list(out, req);
      return 0;
    }
    ht::HotplugMonitor hotplug;  // before the first update: no event can be missed
    update(out, req);
    if (!out.connected()) std::printf("no display connected; waiting for one\n");

    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::seconds(seconds);
    for (;;) {
      int timeout_ms = -1;
      if (seconds > 0) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now());
        if (left.count() <= 0) break;
        timeout_ms = static_cast<int>(left.count());
      }
      pollfd fds[2] = {{sfd, POLLIN, 0}, {hotplug.fd(), POLLIN, 0}};
      const int n = poll(fds, 2, timeout_ms);
      if (n < 0 && errno == EINTR) continue;
      if (n < 0) throw std::runtime_error(std::string("poll: ") + std::strerror(errno));
      if (fds[0].revents & POLLIN) break;
      if ((fds[1].revents & POLLIN) && hotplug.drain()) {
        out.probe();
        update(out, req);
      }
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
  return 0;
}
