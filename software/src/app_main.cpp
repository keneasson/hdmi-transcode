// hdmi-transcode: HDMI in (TC358743 + ISP) -> RGA scale/convert -> HDMI out (DRM/KMS).
//
//   hdmi-transcode [--settings FILE] [--port N] [--mode WxH[@Hz]] [--aspect MODE]
//                  [--snapshot FILE.ppm]
//
// Settings (R5) are loaded from FILE (default /var/lib/hdmi-transcode/settings) and
// changed from the configuration page served on port N (default 80; R9, R12). --mode and
// --aspect override the saved settings for this run only (development).
//
// One event loop, one owner of every device (N8). The output mode is chosen from the
// display and the settings, never from the input (R8a); with no input the test pattern is
// shown (R8). Runs until SIGINT/SIGTERM.
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <poll.h>
#include <rga/rga.h>
#include <sys/signalfd.h>

#include "aspect.h"
#include "capture.h"
#include "drm_output.h"
#include "edid.h"
#include "hotplug.h"
#include "http_server.h"
#include "mode_select.h"
#include "rga_scaler.h"
#include "settings.h"
#include "test_pattern.h"
#include "web_page.h"

namespace {

using Clock = std::chrono::steady_clock;

// No frame for this long while streaming counts as signal loss.
constexpr auto kFrameTimeout = std::chrono::milliseconds(500);
// How often to look for a signal while there is none.
constexpr int kNoSignalRetryMs = 1000;
constexpr auto kStatsEvery = std::chrono::seconds(10);

struct Options {
  std::string settings_path = "/var/lib/hdmi-transcode/settings";
  int port = 80;
  std::optional<ht::ModeRequest> mode_override;
  std::optional<ht::AspectMode> aspect_override;
  std::string snapshot;  // empty = off
};

// Writes an XRGB8888 surface as a binary PPM.
void write_ppm(const std::string& path, const ht::Surface& s) {
  std::ofstream f(path, std::ios::binary);
  f << "P6 " << s.width << " " << s.height << " 255\n";
  std::string row(static_cast<size_t>(s.width) * 3, '\0');
  for (int y = 0; y < s.height; ++y) {
    const uint32_t* p = s.pixels + static_cast<size_t>(y) * s.stride_px;
    for (int x = 0; x < s.width; ++x) {
      row[x * 3] = static_cast<char>(p[x] >> 16);
      row[x * 3 + 1] = static_cast<char>(p[x] >> 8);
      row[x * 3 + 2] = static_cast<char>(p[x]);
    }
    f.write(row.data(), static_cast<std::streamsize>(row.size()));
  }
}

int usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--settings FILE] [--port N] [--mode WxH[@Hz]]"
               " [--aspect compress|letterbox|crop|native] [--snapshot FILE.ppm]\n",
               argv0);
  return 2;
}

ht::HttpResponse json(int status, const std::string& body) {
  return {status, "application/json", body};
}

class App {
 public:
  explicit App(Options o)
      : opt_(std::move(o)),
        settings_(ht::load_settings(opt_.settings_path)),
        web_(static_cast<uint16_t>(opt_.port),
             [this](const ht::HttpRequest& r) { return handle(r); }) {
    if (opt_.mode_override) settings_.mode = opt_.mode_override;
    if (opt_.aspect_override) settings_.aspect = *opt_.aspect_override;
    std::printf("settings: mode %s, aspect %s, test pattern %s (web page on port %d)\n",
                ht::mode_to_string(settings_.mode).c_str(), ht::to_string(settings_.aspect),
                settings_.force_pattern ? "on" : "off", opt_.port);
  }

  int run(int sfd) {
    update_output();
    restart_capture();
    std::vector<pollfd> fds;
    for (;;) {
      fds = {{sfd, POLLIN, 0},
             {hotplug_.fd(), POLLIN, 0},
             {out_.fd(), POLLIN, 0},
             {cap_.subdev_fd(), POLLPRI, 0},
             {cap_.video_fd(), static_cast<short>(cap_.streaming() ? POLLIN : 0), 0}};
      const size_t web_first = fds.size();
      web_.add_pollfds(fds);
      const int n = poll(fds.data(), fds.size(), timeout_ms());
      if (n < 0 && errno == EINTR) continue;
      if (n < 0) throw std::runtime_error(std::string("poll: ") + std::strerror(errno));

      if (fds[0].revents & POLLIN) return 0;
      if (fds[2].revents & POLLIN) out_.handle_events();
      if ((fds[1].revents & POLLIN) && hotplug_.drain()) {
        out_.probe();
        update_output();
      }
      if ((fds[3].revents & POLLPRI) && cap_.source_changed()) {
        std::printf("input changed\n");
        restart_capture();
      }
      if (fds[4].revents & POLLIN) on_frames();
      web_.on_events(fds, web_first);

      if (Clock::now() >= next_stats_) log_stats();
      if (cap_.streaming() && Clock::now() - last_frame_ > kFrameTimeout) {
        std::printf("input %s: no frames, treating as signal loss\n",
                    cap_.description().c_str());
        restart_capture();
      } else if (!cap_.streaming() && Clock::now() >= next_retry_) {
        restart_capture();
      }
    }
  }

 private:
  int timeout_ms() const {
    const auto now = Clock::now();
    auto until = cap_.streaming() ? last_frame_ + kFrameTimeout : next_retry_;
    if (next_stats_ < until) until = next_stats_;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(until - now).count();
    ms = ms < 0 ? 0 : ms + 1;
    const int web = web_.next_timeout_ms();
    if (web >= 0 && web < ms) ms = web;
    return static_cast<int>(ms);
  }

  void log_stats() {
    const double secs =
        std::chrono::duration<double>(Clock::now() - (next_stats_ - kStatsEvery)).count();
    fps_in_ = captured_ / secs;
    if (captured_)
      std::printf("stats: in %.1f fps, shown %.1f fps, rga %.2f ms avg / %.2f ms max\n",
                  captured_ / secs, shown_ / secs, shown_ ? rga_us_total_ / 1000.0 / shown_ : 0,
                  rga_us_max_ / 1000.0);
    captured_ = shown_ = 0;
    rga_us_total_ = rga_us_max_ = 0;
    next_stats_ = Clock::now() + kStatsEvery;
  }

  // Display side: (re)choose the mode only when the display or the setting changes (R2,
  // R2b, R8a).
  void update_output() {
    if (!out_.connected()) {
      if (out_.active()) {
        out_.blank();
        std::printf("display disconnected; output off until one is connected\n");
      }
      return;
    }
    const ht::ModeChoice c =
        ht::choose_mode(out_.modes(), ht::edid_preferred_timing(out_.edid()), settings_.mode);
    mode_warning_ = settings_.mode && c.reason.rfind("requested", 0) != 0
                        ? "The projector does not offer " + ht::mode_to_string(settings_.mode) +
                              "; using " + ht::describe(c.mode) + "."
                        : "";
    if (out_.active() && ht::same_timing(out_.mode(), c.mode)) return;
    out_.set_mode(c.mode);
    out_.keep_awake();
    pattern_on_ = false;
    std::printf("output %s (%s), aspect %s\n", ht::describe(c.mode).c_str(), c.reason.c_str(),
                ht::to_string(settings_.aspect));
    if (!cap_.streaming() || settings_.force_pattern) show_pattern();
  }

  // Input side: stop fully, look for a signal, start again (N8, R6).
  void restart_capture() {
    cap_.stop();
    const auto t = cap_.detect();
    if (!t || t->interlaced) {
      if (had_signal_ || first_) std::printf("no input signal; showing the test pattern\n");
      had_signal_ = false;
      first_ = false;
      show_pattern();
      next_retry_ = Clock::now() + std::chrono::milliseconds(kNoSignalRetryMs);
      return;
    }
    try {
      cap_.start(*t);
    } catch (const std::exception& e) {
      std::printf("input %dx%d: cannot capture (%s); showing the test pattern\n", t->width,
                  t->height, e.what());
      show_pattern();
      next_retry_ = Clock::now() + std::chrono::milliseconds(kNoSignalRetryMs);
      return;
    }
    had_signal_ = true;
    first_ = false;
    last_frame_ = Clock::now();
    std::printf("input %s\n", cap_.description().c_str());
  }

  void show_pattern() {
    if (!out_.active() || pattern_on_) return;
    pattern_on_ = true;
    const int i = out_.acquire();
    ht::Surface s = out_.surface(i);
    const char* why = settings_.force_pattern ? "  TEST PATTERN" : "  NO INPUT";
    ht::draw_test_pattern(s, ht::describe(out_.mode()) + why);
    out_.present(i);
  }

  // Take every ready frame, show only the newest (latency over completeness).
  void on_frames() {
    std::optional<ht::CapturedFrame> latest;
    while (auto f = cap_.dequeue()) {
      ++captured_;
      if (latest) cap_.requeue(latest->index);
      latest = f;
    }
    if (!latest) return;
    last_frame_ = Clock::now();
    if (out_.active() && !settings_.force_pattern) {
      const int i = out_.acquire();
      const ht::DmaImage src{latest->dmabuf_fd,   latest->width, latest->height,
                             latest->stride_bytes, latest->plane_height,
                             RK_FORMAT_YCbCr_420_SP};
      const int w = out_.mode().hdisplay, h = out_.mode().vdisplay;
      const ht::DmaImage dst{out_.dmabuf_fd(i), w, h, out_.stride_bytes(i) / 4, h,
                             RK_FORMAT_BGRX_8888};
      const auto matrix = latest->height >= 720 ? ht::YuvMatrix::Bt709Limited
                                                : ht::YuvMatrix::Bt601Limited;
      const auto t0 = Clock::now();
      rga_.blit(src, dst, ht::place(latest->width, latest->height, w, h, settings_.aspect),
                matrix);
      const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0)
                           .count();
      rga_us_total_ += us;
      if (us > rga_us_max_) rga_us_max_ = us;
      if (!opt_.snapshot.empty() && ++snapshot_count_ == 120) {
        write_ppm(opt_.snapshot, out_.surface(i));
        std::printf("snapshot written to %s\n", opt_.snapshot.c_str());
      }
      out_.present(i);
      ++shown_;
      pattern_on_ = false;
    } else if (settings_.force_pattern) {
      show_pattern();
    }
    cap_.requeue(latest->index);
  }

  // --- configuration page (R9, R12) ------------------------------------------------------
  ht::HttpResponse handle(const ht::HttpRequest& r) {
    if (r.path == "/" && r.method == "GET") return {200, "text/html; charset=utf-8", ht::kConfigPage};
    if (r.path == "/api/status" && r.method == "GET") return json(200, status_json());
    if (r.path == "/api/settings") {
      if (r.method != "POST") return json(405, R"({"error":"use POST"})");
      // Cross-site requests can't set custom headers without a CORS preflight, which this
      // server never approves: a page on another site cannot change the box (R11a).
      if (r.headers.count("x-ht-request") == 0)
        return json(403, R"({"error":"missing X-HT-Request header"})");
      return apply_settings(ht::parse_form(r.body));
    }
    return {404, "text/plain; charset=utf-8", "not found\n"};
  }

  ht::HttpResponse apply_settings(const std::map<std::string, std::string>& f) {
    ht::Settings next = settings_;
    if (auto it = f.find("aspect"); it != f.end()) {
      const auto a = ht::parse_aspect(it->second);
      if (!a) return json(400, R"({"error":"unknown picture shape"})");
      next.aspect = *a;
    }
    if (auto it = f.find("mode"); it != f.end()) {
      if (it->second == "auto") {
        next.mode.reset();
      } else {
        const auto m = ht::parse_mode(it->second);
        if (!m) return json(400, R"({"error":"unknown resolution"})");
        next.mode = m;
      }
    }
    if (auto it = f.find("test_pattern"); it != f.end()) {
      if (it->second != "on" && it->second != "off")
        return json(400, R"({"error":"test_pattern must be on or off"})");
      next.force_pattern = it->second == "on";
    }
    if (!(next == settings_)) {
      const bool mode_changed = ht::mode_to_string(next.mode) != ht::mode_to_string(settings_.mode);
      const bool pattern_changed = next.force_pattern != settings_.force_pattern;
      ht::save_settings(opt_.settings_path, next);  // R5: persist before reporting success
      settings_ = next;
      std::printf("settings changed: mode %s, aspect %s, test pattern %s\n",
                  ht::mode_to_string(settings_.mode).c_str(), ht::to_string(settings_.aspect),
                  settings_.force_pattern ? "on" : "off");
      if (mode_changed) update_output();
      if (pattern_changed) {
        pattern_on_ = false;
        if (settings_.force_pattern || !cap_.streaming()) show_pattern();
      }
    }
    return json(200, status_json());
  }

  std::string status_json() const {
    std::string s = "{";
    s += "\"input\":";
    s += cap_.streaming() ? "\"" + ht::json_escape(cap_.description()) + "\"" : "null";
    s += ",\"output\":";
    s += out_.active() ? "\"" + ht::json_escape(ht::describe(out_.mode())) + "\"" : "null";
    s += ",\"aspect\":\"" + std::string(ht::to_string(settings_.aspect)) + "\"";
    s += ",\"mode\":\"" + ht::json_escape(ht::mode_to_string(settings_.mode)) + "\"";
    s += std::string(",\"test_pattern\":") + (settings_.force_pattern ? "true" : "false");
    s += ",\"fps_in\":" + std::to_string(static_cast<int>(fps_in_ + 0.5));
    if (!mode_warning_.empty()) s += ",\"warning\":\"" + ht::json_escape(mode_warning_) + "\"";
    // Choices: automatic, what the projector offers, our built-in fallbacks.
    s += ",\"modes\":[{\"value\":\"auto\",\"label\":\"Automatic (recommended)\"}";
    std::set<std::string> seen{"auto"};
    auto add = [&](const drmModeModeInfo& m) {
      if (m.flags & DRM_MODE_FLAG_INTERLACE) return;
      const std::string value = std::to_string(m.hdisplay) + "x" + std::to_string(m.vdisplay) +
                                "@" + std::to_string((ht::refresh_mhz(m) + 500) / 1000);
      if (!seen.insert(value).second) return;
      s += ",{\"value\":\"" + value + "\",\"label\":\"" + ht::json_escape(ht::describe(m)) + "\"}";
    };
    for (const auto& m : out_.modes()) add(m);
    for (const auto& m : ht::fallback_modes()) add(m);
    if (settings_.mode && !seen.count(ht::mode_to_string(settings_.mode)))
      s += ",{\"value\":\"" + ht::json_escape(ht::mode_to_string(settings_.mode)) +
           "\",\"label\":\"" + ht::json_escape(ht::mode_to_string(settings_.mode)) +
           " (saved)\"}";
    s += "]}";
    return s;
  }

  Options opt_;
  ht::Settings settings_;
  ht::DrmOutput out_;
  ht::HotplugMonitor hotplug_;
  ht::Capture cap_;
  ht::RgaScaler rga_;
  ht::HttpServer web_;
  Clock::time_point last_frame_{};
  Clock::time_point next_retry_{};
  bool had_signal_ = false;
  bool first_ = true;
  bool pattern_on_ = false;  // the test pattern is what's on screen; don't redraw it
  std::string mode_warning_;
  Clock::time_point next_stats_ = Clock::now() + kStatsEvery;
  long captured_ = 0, shown_ = 0, snapshot_count_ = 0;
  long long rga_us_total_ = 0, rga_us_max_ = 0;
  double fps_in_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  Options opt;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--mode" && i + 1 < argc) {
      opt.mode_override = ht::parse_mode(argv[++i]);
      if (!opt.mode_override) return usage(argv[0]);
    } else if (a == "--aspect" && i + 1 < argc) {
      opt.aspect_override = ht::parse_aspect(argv[++i]);
      if (!opt.aspect_override) return usage(argv[0]);
    } else if (a == "--settings" && i + 1 < argc) {
      opt.settings_path = argv[++i];
    } else if (a == "--port" && i + 1 < argc) {
      opt.port = std::atoi(argv[++i]);
      if (opt.port <= 0 || opt.port > 65535) return usage(argv[0]);
    } else if (a == "--snapshot" && i + 1 < argc) {
      opt.snapshot = argv[++i];
    } else {
      return usage(argv[0]);
    }
  }

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
    App app(opt);
    return app.run(sfd);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
