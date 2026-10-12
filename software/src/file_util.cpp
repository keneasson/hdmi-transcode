#include "file_util.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ht {

void write_file_atomic(const std::string& path, const std::string& data) {
  const std::string tmp = path + ".tmp";
  const auto slash = path.rfind('/');
  const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
  ::mkdir(dir.c_str(), 0755);  // first save on a fresh system; EEXIST is fine
  const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) throw std::runtime_error("save " + tmp + ": " + std::strerror(errno));
  bool ok = true;
  for (size_t done = 0; ok && done < data.size();) {
    const ssize_t w = ::write(fd, data.data() + done, data.size() - done);
    if (w < 0 && errno == EINTR) continue;
    ok = w > 0;
    if (ok) done += static_cast<size_t>(w);
  }
  ok = ok && ::fsync(fd) == 0;
  const int err = errno;
  ::close(fd);
  if (!ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
    const int e = ok ? errno : err;
    ::unlink(tmp.c_str());
    throw std::runtime_error("save " + path + ": " + std::strerror(e));
  }
  const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dfd >= 0) {
    ::fsync(dfd);  // make the rename itself durable
    ::close(dfd);
  }
}

std::optional<std::string> read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::nullopt;
  std::stringstream ss;
  ss << f.rdbuf();
  if (f.bad()) return std::nullopt;
  return ss.str();
}

}  // namespace ht
