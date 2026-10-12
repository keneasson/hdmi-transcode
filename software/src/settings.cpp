#include "settings.h"

#include <sstream>

#include "file_util.h"

namespace ht {

const char* to_string(Show s) {
  switch (s) {
    case Show::Input: return "input";
    case Show::Slide: return "slide";
    case Show::Pattern: return "pattern";
  }
  return "input";
}

std::optional<Show> parse_show(const std::string& s) {
  if (s == "input") return Show::Input;
  if (s == "slide") return Show::Slide;
  if (s == "pattern") return Show::Pattern;
  return std::nullopt;
}

bool Settings::operator==(const Settings& o) const {
  const bool same_mode =
      mode.has_value() == o.mode.has_value() &&
      (!mode || (mode->width == o.mode->width && mode->height == o.mode->height &&
                 mode->refresh == o.mode->refresh));
  return same_mode && aspect == o.aspect && show == o.show;
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
  out += std::string("show=") + to_string(s.show) + "\n";
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
    } else if (key == "show") {
      if (auto v = parse_show(value)) s.show = *v;
    } else if (key == "test_pattern") {  // written before `show` existed
      if (value == "on") s.show = Show::Pattern;
      if (value == "off") s.show = Show::Input;
    }
  }
  return s;
}

Settings load_settings(const std::string& path) {
  const auto text = read_file(path);
  return text ? parse_settings(*text) : Settings{};
}

void save_settings(const std::string& path, const Settings& s) {
  write_file_atomic(path, serialize(s));
}

}  // namespace ht
