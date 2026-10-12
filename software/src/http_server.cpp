#include "http_server.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ht {
namespace {

constexpr size_t kMaxClients = 8;
constexpr auto kIdle = std::chrono::seconds(5);

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t");
  if (b == std::string::npos) return {};
  return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}

const char* reason(int status) {
  switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    default: return status >= 500 ? "Internal Server Error" : "Error";
  }
}

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string url_decode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') {
      out += ' ';
    } else if (s[i] == '%' && i + 2 < s.size() && hexval(s[i + 1]) >= 0 &&
               hexval(s[i + 2]) >= 0) {
      out += static_cast<char>(hexval(s[i + 1]) * 16 + hexval(s[i + 2]));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

}  // namespace

bool is_private_ipv4(uint32_t a) {
  return (a >> 24) == 10 || (a >> 20) == ((172u << 4) | 1) || (a >> 16) == ((192u << 8) | 168) ||
         (a >> 24) == 127 || (a >> 16) == ((169u << 8) | 254);
}

std::map<std::string, std::string> parse_form(const std::string& s) {
  std::map<std::string, std::string> m;
  size_t pos = 0;
  while (pos <= s.size()) {
    const size_t amp = std::min(s.find('&', pos), s.size());
    const std::string pair = s.substr(pos, amp - pos);
    if (!pair.empty()) {
      const size_t eq = pair.find('=');
      if (eq == std::string::npos)
        m[url_decode(pair)] = "";
      else
        m[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
    }
    pos = amp + 1;
  }
  return m;
}

std::string json_escape(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char b[8];
          std::snprintf(b, sizeof b, "\\u%04x", c);
          out += b;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

ParseResult parse_http_request(const std::string& buf, HttpRequest& out) {
  const size_t hdr_end = buf.find("\r\n\r\n");
  if (hdr_end == std::string::npos)
    return buf.size() > kMaxRequestBytes ? ParseResult::TooLarge : ParseResult::Incomplete;
  if (hdr_end + 4 > kMaxRequestBytes) return ParseResult::TooLarge;

  HttpRequest r;
  size_t line_end = buf.find("\r\n");
  const std::string request_line = buf.substr(0, line_end);
  const size_t sp1 = request_line.find(' ');
  const size_t sp2 = sp1 == std::string::npos ? sp1 : request_line.find(' ', sp1 + 1);
  if (sp1 == std::string::npos || sp2 == std::string::npos) return ParseResult::Bad;
  r.method = request_line.substr(0, sp1);
  std::string target = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
  if (request_line.compare(sp2 + 1, 5, "HTTP/") != 0 || target.empty() || target[0] != '/')
    return ParseResult::Bad;
  const size_t q = target.find('?');
  r.path = target.substr(0, q);
  if (q != std::string::npos) r.query = target.substr(q + 1);

  size_t pos = line_end + 2;
  while (pos < hdr_end) {
    const size_t e = buf.find("\r\n", pos);
    const std::string line = buf.substr(pos, e - pos);
    const size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) return ParseResult::Bad;
    r.headers[lower(line.substr(0, colon))] = trim(line.substr(colon + 1));
    pos = e + 2;
  }

  size_t length = 0;
  if (auto it = r.headers.find("content-length"); it != r.headers.end()) {
    const std::string& v = it->second;
    if (v.empty() || v.size() > 6 || v.find_first_not_of("0123456789") != std::string::npos)
      return ParseResult::Bad;
    length = std::stoul(v);
  }
  if (r.headers.count("transfer-encoding")) return ParseResult::Bad;  // not supported
  if (hdr_end + 4 + length > kMaxRequestBytes) return ParseResult::TooLarge;
  if (buf.size() < hdr_end + 4 + length) return ParseResult::Incomplete;
  r.body = buf.substr(hdr_end + 4, length);
  out = std::move(r);
  return ParseResult::Ok;
}

HttpServer::HttpServer(uint16_t port, HttpHandler handler) : handler_(std::move(handler)) {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (listen_fd_ < 0) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
  const int one = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0 ||
      ::listen(listen_fd_, 16) < 0) {
    const int err = errno;
    ::close(listen_fd_);
    throw std::runtime_error("listen on port " + std::to_string(port) + ": " +
                             std::strerror(err));
  }
}

HttpServer::~HttpServer() {
  for (auto& c : clients_) ::close(c.fd);
  ::close(listen_fd_);
}

void HttpServer::add_pollfds(std::vector<pollfd>& fds) const {
  fds.push_back({listen_fd_, POLLIN, 0});
  for (const auto& c : clients_)
    fds.push_back({c.fd, static_cast<short>(c.out.empty() ? POLLIN : POLLOUT), 0});
}

int HttpServer::next_timeout_ms() const {
  if (clients_.empty()) return -1;
  auto soonest = clients_.front().deadline;
  for (const auto& c : clients_) soonest = std::min(soonest, c.deadline);
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(soonest - Clock::now()).count();
  return ms < 0 ? 0 : static_cast<int>(ms) + 1;
}

void HttpServer::on_events(const std::vector<pollfd>& fds, size_t first) {
  // Clients first (their fds follow the listener in the order add_pollfds() wrote them),
  // walking backwards so closing one doesn't shift the ones still to visit.
  const size_t n = std::min(clients_.size(), fds.size() - first - 1);
  for (size_t k = n; k-- > 0;) {
    const short ev = fds[first + 1 + k].revents;
    if (ev && fds[first + 1 + k].fd == clients_[k].fd) {
      on_client(clients_[k], ev);
      if (clients_[k].fd < 0) close_client(k);
    }
  }
  if (fds[first].revents & POLLIN) accept_clients();
  expire();
}

void HttpServer::accept_clients() {
  for (;;) {
    sockaddr_in a{};
    socklen_t len = sizeof a;
    const int fd = ::accept4(listen_fd_, reinterpret_cast<sockaddr*>(&a), &len,
                             SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0) return;  // EAGAIN or a transient error: nothing more to accept now
    if (a.sin_family != AF_INET || !is_private_ipv4(ntohl(a.sin_addr.s_addr)) ||
        clients_.size() >= kMaxClients) {
      ::close(fd);  // R11a: never serve non-private peers; bounded clients
      continue;
    }
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
    clients_.push_back({fd, ip, {}, {}, 0, Clock::now() + kIdle});
  }
}

void HttpServer::on_client(Client& c, short revents) {
  if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
    ::close(c.fd);
    c.fd = -1;
    return;
  }
  if (c.out.empty() && (revents & POLLIN)) {
    char buf[2048];
    for (;;) {
      const ssize_t r = ::recv(c.fd, buf, sizeof buf, 0);
      if (r > 0) {
        c.in.append(buf, static_cast<size_t>(r));
        if (c.in.size() > kMaxRequestBytes + 1) break;
        continue;
      }
      if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
        ::close(c.fd);
        c.fd = -1;
        return;
      }
      break;
    }
    HttpRequest req;
    switch (parse_http_request(c.in, req)) {
      case ParseResult::Incomplete: return;
      case ParseResult::Bad: respond(c, {400, "text/plain; charset=utf-8", "bad request\n"}); break;
      case ParseResult::TooLarge: respond(c, {413, "text/plain; charset=utf-8", "too large\n"}); break;
      case ParseResult::Ok:
        req.peer = c.peer;
        try {
          respond(c, handler_(req));
        } catch (const std::exception& e) {
          respond(c, {500, "text/plain; charset=utf-8", std::string(e.what()) + "\n"});
        }
        break;
    }
  }
  if (!c.out.empty()) {
    while (c.sent < c.out.size()) {
      const ssize_t w = ::send(c.fd, c.out.data() + c.sent, c.out.size() - c.sent, MSG_NOSIGNAL);
      if (w > 0) {
        c.sent += static_cast<size_t>(w);
        continue;
      }
      if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
      break;
    }
    ::close(c.fd);  // done (or failed): one request per connection
    c.fd = -1;
  }
}

void HttpServer::respond(Client& c, const HttpResponse& r) {
  char head[256];
  std::snprintf(head, sizeof head,
                "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                "Connection: close\r\n\r\n",
                r.status, reason(r.status), r.content_type.c_str(), r.body.size());
  c.out = head + r.body;
  c.sent = 0;
  c.deadline = Clock::now() + kIdle;
}

void HttpServer::close_client(size_t i) {
  if (clients_[i].fd >= 0) ::close(clients_[i].fd);
  clients_.erase(clients_.begin() + static_cast<long>(i));
}

void HttpServer::expire() {
  const auto now = Clock::now();
  for (size_t i = clients_.size(); i-- > 0;)
    if (clients_[i].fd < 0 || now >= clients_[i].deadline) close_client(i);
}

}  // namespace ht
