#include "settings.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ht {

bool Settings::operator==(const Settings& o) const {
  const bool same_mode =
      mode.has_value() == o.mode.has_value() &&
      (!mode || (mode->width == o.mode->width && mode->height == o.mode->height &&
                 mode->refresh == o.mode->refresh));
  return same_mode && aspect == o.aspect && force_pattern == o.force_pattern;
}

std::string mode_to_string(const std::optional<ModeRequest>& m) {
  if (!m) return "auto";
  std::string s = std::to_string(m->width) + "x" + std::to_string(m->height);
  if (m->refresh) s += "@" + std::to_string(m->refresh);
  return s;
}

std::string serialize(const Settings& s) {
  std::string out = "# hdmi-transcode settings (written by the box; edit with care)\n";
  out += "mode=" + mode_to_string(s.mode) + "\n";
  out += std::string("aspect=") + to_string(s.aspect) + "\n";
  out += std::string("test_pattern=") + (s.force_pattern ? "on" : "off") + "\n";
  return out;
}

Settings parse_settings(const std::string& text) {
  Settings s;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
    if (key == "mode") {
      if (value == "auto")
        s.mode.reset();
      else if (auto m = parse_mode(value))
        s.mode = m;
    } else if (key == "aspect") {
      if (auto a = parse_aspect(value)) s.aspect = *a;
    } else if (key == "test_pattern") {
      if (value == "on") s.force_pattern = true;
      if (value == "off") s.force_pattern = false;
    }
  }
  return s;
}

Settings load_settings(const std::string& path) {
  std::ifstream f(path);
  if (!f) return {};
  std::stringstream ss;
  ss << f.rdbuf();
  return parse_settings(ss.str());
}

void save_settings(const std::string& path, const Settings& s) {
  const std::string tmp = path + ".tmp";
  const std::string data = serialize(s);
  const auto slash = path.rfind('/');
  const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
  ::mkdir(dir.c_str(), 0755);  // first save on a fresh system; EEXIST is fine
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) throw std::runtime_error("save settings: " + tmp + ": " + std::strerror(errno));
  const bool ok = ::write(fd, data.data(), data.size()) == static_cast<ssize_t>(data.size()) &&
                  ::fsync(fd) == 0;
  const int err = errno;
  ::close(fd);
  if (!ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
    ::unlink(tmp.c_str());
    throw std::runtime_error("save settings: " + path + ": " + std::strerror(ok ? errno : err));
  }
  const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dfd >= 0) {
    ::fsync(dfd);  // make the rename itself durable
    ::close(dfd);
  }
}

}  // namespace ht
