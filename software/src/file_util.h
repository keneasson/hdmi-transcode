// Small file helpers shared by the settings (R5) and slide (R13) stores.
#pragma once

#include <optional>
#include <string>

namespace ht {

// Atomic replace: write `path`.tmp, fsync, rename over `path`, fsync the directory, so a
// power cut leaves either the old or the new file, never a broken one. Creates the
// directory if it is missing. Throws std::runtime_error.
void write_file_atomic(const std::string& path, const std::string& data);

// Whole file, or nullopt if it is missing or unreadable.
std::optional<std::string> read_file(const std::string& path);

}  // namespace ht
