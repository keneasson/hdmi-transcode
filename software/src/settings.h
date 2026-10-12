// Persistent user settings (requirement R5): the box comes back up unattended in the last
// configuration. Stored as a small `key=value` text file, written atomically so a power
// cut during a save leaves either the old or the new file, never a broken one.
#pragma once

#include <optional>
#include <string>

#include "aspect.h"
#include "mode_select.h"

namespace ht {

struct Settings {
  std::optional<ModeRequest> mode;  // nullopt = automatic (R2/R2a)
  AspectMode aspect = AspectMode::Compress;
  bool force_pattern = false;  // show the test pattern even with an input (R12)

  bool operator==(const Settings& o) const;
};

// Text form, one `key=value` per line. Unknown keys and malformed values are ignored
// (keeping the default), so a file from an older or newer version still loads.
std::string serialize(const Settings& s);
Settings parse_settings(const std::string& text);

// Missing or unreadable file -> defaults.
Settings load_settings(const std::string& path);
// Atomic replace (write temp, fsync, rename, fsync dir). Throws std::runtime_error.
void save_settings(const std::string& path, const Settings& s);

std::string mode_to_string(const std::optional<ModeRequest>& m);  // "auto" or "WxH@Hz"

}  // namespace ht
