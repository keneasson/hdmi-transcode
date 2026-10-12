// Minimal HTTP/1.1 server for the configuration page (requirements R9, R11a), driven by
// the application's own poll loop: no threads, no extra daemon.
//
// Deliberately small and strict: one request per connection (Connection: close), request
// size capped, a bounded number of clients, idle clients dropped, and connections from
// anything but private / link-local / loopback IPv4 addresses refused (R11a).
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <poll.h>

namespace ht {

struct HttpRequest {
  std::string method, path, query;
  std::map<std::string, std::string> headers;  // names lower-cased
  std::string body;
  std::string peer;  // dotted IPv4
};

struct HttpResponse {
  int status = 200;
  std::string content_type = "text/plain; charset=utf-8";
  std::string body;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

// --- pure helpers (unit-tested) ---------------------------------------------------------
constexpr size_t kMaxRequestBytes = 8192;

enum class ParseResult { Incomplete, Ok, Bad, TooLarge };
// Parses one request from `buf`. On Ok fills `out` (method, path, query, headers, body).
ParseResult parse_http_request(const std::string& buf, HttpRequest& out);
// RFC 1918, 127/8, 169.254/16. `addr` in host byte order.
bool is_private_ipv4(uint32_t addr);
// application/x-www-form-urlencoded -> map ('+' is space, %XX decoded, bad escapes kept).
std::map<std::string, std::string> parse_form(const std::string& s);
std::string json_escape(const std::string& s);

class HttpServer {
 public:
  // Listens on 0.0.0.0:port. Throws std::runtime_error.
  HttpServer(uint16_t port, HttpHandler handler);
  ~HttpServer();
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

  // Appends the fds to poll (listener + clients); remember where they start.
  void add_pollfds(std::vector<pollfd>& fds) const;
  // Handles events for the entries this server appended, starting at `first`.
  void on_events(const std::vector<pollfd>& fds, size_t first);
  // Milliseconds until the next idle timeout, or -1.
  int next_timeout_ms() const;

 private:
  using Clock = std::chrono::steady_clock;
  struct Client {
    int fd = -1;
    std::string peer, in, out;
    size_t sent = 0;
    Clock::time_point deadline;
  };
  void accept_clients();
  void on_client(Client& c, short revents);
  void respond(Client& c, const HttpResponse& r);
  void close_client(size_t i);
  void expire();

  int listen_fd_ = -1;
  HttpHandler handler_;
  std::vector<Client> clients_;
};

}  // namespace ht
