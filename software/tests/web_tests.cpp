// Tests for settings persistence (R5) and the config server's pure parts (R9, R11a).
#include <cstdio>
#include <string>
#include <unistd.h>

#include "check.h"
#include "event_log.h"
#include "http_server.h"
#include "settings.h"

namespace {

void settings_round_trip() {
  ht::Settings s;
  s.mode = ht::ModeRequest{1280, 800, 60};
  s.aspect = ht::AspectMode::Letterbox;
  s.show = ht::Show::Slide;
  CHECK(ht::parse_settings(ht::serialize(s)) == s);
  ht::Settings d;  // defaults: auto, compress, pattern off
  CHECK(ht::parse_settings(ht::serialize(d)) == d);
  CHECK(ht::mode_to_string(std::nullopt) == "auto");
  CHECK(ht::mode_to_string(ht::ModeRequest{1024, 768, 0}) == "1024x768");
  for (auto v : {ht::Show::Input, ht::Show::Slide, ht::Show::Pattern})
    CHECK(ht::parse_show(ht::to_string(v)) == v);
  CHECK(!ht::parse_show("bogus"));
  // Files written before `show` existed.
  CHECK(ht::parse_settings("test_pattern=on\n").show == ht::Show::Pattern);
  CHECK(ht::parse_settings("test_pattern=off\n").show == ht::Show::Input);
  CHECK(ht::parse_settings("show=slide\n").show == ht::Show::Slide);
}

void settings_tolerate_junk() {
  auto s = ht::parse_settings("mode=garbage\naspect=sideways\ntest_pattern=maybe\nfuture=1\n"
                              "no equals here\n\r\n# comment\n");
  CHECK(s == ht::Settings{});
  s = ht::parse_settings("aspect=crop\r\nmode=1024x768@60\r\n");  // CRLF from a Windows edit
  CHECK(s.aspect == ht::AspectMode::Crop && s.mode && s.mode->width == 1024);
}

void settings_file_io() {
  char dir[] = "/tmp/ht-settings-XXXXXX";
  CHECK(mkdtemp(dir) != nullptr);
  const std::string path = std::string(dir) + "/settings";
  CHECK(ht::load_settings(path) == ht::Settings{});  // missing file -> defaults
  ht::Settings s;
  s.aspect = ht::AspectMode::Crop;
  ht::save_settings(path, s);
  CHECK(ht::load_settings(path) == s);
  CHECK(access((path + ".tmp").c_str(), F_OK) != 0);  // temp file renamed away
  unlink(path.c_str());
  rmdir(dir);
}

void http_parsing() {
  ht::HttpRequest r;
  CHECK(ht::parse_http_request("GET / HTTP/1.1\r\nHost: x\r\n", r) == ht::ParseResult::Incomplete);
  CHECK(ht::parse_http_request("GET /api/status?x=1 HTTP/1.1\r\nHost: x\r\n\r\n", r) ==
        ht::ParseResult::Ok);
  CHECK(r.method == "GET" && r.path == "/api/status" && r.query == "x=1");
  CHECK(r.headers["host"] == "x");

  const std::string post = "POST /api/settings HTTP/1.1\r\nContent-Length: 13\r\n"
                           "X-HT-Request: 1\r\n\r\naspect=crop&x";
  CHECK(ht::parse_http_request(post.substr(0, post.size() - 3), r) == ht::ParseResult::Incomplete);
  CHECK(ht::parse_http_request(post, r) == ht::ParseResult::Ok);
  CHECK(r.body == "aspect=crop&x" && r.headers.count("x-ht-request"));

  CHECK(ht::parse_http_request("GARBAGE\r\n\r\n", r) == ht::ParseResult::Bad);
  CHECK(ht::parse_http_request("GET nopath HTTP/1.1\r\n\r\n", r) == ht::ParseResult::Bad);
  CHECK(ht::parse_http_request("GET / HTTP/1.1\r\nContent-Length: -1\r\n\r\n", r) ==
        ht::ParseResult::Bad);
  CHECK(ht::parse_http_request("GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", r) ==
        ht::ParseResult::Bad);
  CHECK(ht::parse_http_request("POST / HTTP/1.1\r\nContent-Length: 999999\r\n\r\n", r) ==
        ht::ParseResult::TooLarge);
  CHECK(ht::parse_http_request(std::string(9000, 'a'), r) == ht::ParseResult::TooLarge);

  // A larger body only where allowed, and only up to that path's limit.
  const ht::BodyLimits limits{{"/api/slide", 100000}};
  const std::string big = "POST /api/slide HTTP/1.1\r\nContent-Length: 50000\r\n\r\n" +
                          std::string(50000, 'x');
  CHECK(ht::parse_http_request(big, r, limits) == ht::ParseResult::Ok && r.body.size() == 50000);
  CHECK(ht::parse_http_request(big, r) == ht::ParseResult::TooLarge);
  CHECK(ht::parse_http_request("POST /api/settings HTTP/1.1\r\nContent-Length: 50000\r\n\r\n",
                               r, limits) == ht::ParseResult::TooLarge);
  CHECK(ht::parse_http_request("POST /api/slide HTTP/1.1\r\nContent-Length: 200000\r\n\r\n",
                               r, limits) == ht::ParseResult::TooLarge);
}

uint32_t ip(int a, int b, int c, int d) {
  return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(c) << 8) | uint32_t(d);
}

void private_addresses_only() {
  CHECK(ht::is_private_ipv4(ip(10, 0, 0, 165)));
  CHECK(ht::is_private_ipv4(ip(192, 168, 4, 2)));
  CHECK(ht::is_private_ipv4(ip(172, 16, 0, 1)));
  CHECK(ht::is_private_ipv4(ip(172, 31, 255, 255)));
  CHECK(ht::is_private_ipv4(ip(127, 0, 0, 1)));
  CHECK(ht::is_private_ipv4(ip(169, 254, 1, 1)));
  CHECK(!ht::is_private_ipv4(ip(172, 32, 0, 1)));
  CHECK(!ht::is_private_ipv4(ip(8, 8, 8, 8)));
  CHECK(!ht::is_private_ipv4(ip(192, 169, 0, 1)));
  CHECK(!ht::is_private_ipv4(ip(11, 0, 0, 1)));
}

void forms_and_json() {
  auto f = ht::parse_form("aspect=letterbox&mode=1280x800%4060&note=a+b&empty=&flag&bad=%zz");
  CHECK(f["aspect"] == "letterbox");
  CHECK(f["mode"] == "1280x800@60");
  CHECK(f["note"] == "a b");
  CHECK(f.count("empty") && f["empty"].empty());
  CHECK(f.count("flag"));
  CHECK(f["bad"] == "%zz");
  CHECK(ht::json_escape("a\"b\\c\n\x01") == "a\\\"b\\\\c\\n\\u0001");
}

void base64() {
  CHECK(ht::base64_decode("") == std::string());
  CHECK(ht::base64_decode("TWFu") == std::string("Man"));
  CHECK(ht::base64_decode("TWE=") == std::string("Ma"));
  CHECK(ht::base64_decode("TQ==") == std::string("M"));
  CHECK(ht::base64_decode("TQ") == std::string("M"));  // padding optional
  CHECK(ht::base64_decode("+/+/") == std::string("\xfb\xff\xbf"));
  CHECK(!ht::base64_decode("TQ==="));
  CHECK(!ht::base64_decode("T"));
  CHECK(!ht::base64_decode("TW F"));
  CHECK(!ht::base64_decode("TW-u"));
}

}  // namespace

void event_log_keeps_newest_and_escapes() {
  using C = ht::EventLog::Clock;
  const auto t0 = C::now();
  ht::EventLog log(3, t0);
  CHECK(log.json() == "[]");
  log.add_at(t0 + std::chrono::seconds(1), "one");
  log.add_at(t0 + std::chrono::seconds(2), "two \"quoted\"");
  log.add_at(t0 + std::chrono::seconds(5), "three");
  log.add_at(t0 + std::chrono::seconds(9), "four");
  CHECK(log.size() == 3);
  CHECK(log.json() ==
        R"([{"t":2,"text":"two \"quoted\""},{"t":5,"text":"three"},{"t":9,"text":"four"}])");
  ht::EventLog none(0, t0);
  none.add_at(t0, "dropped");
  CHECK(none.size() == 0);
}

int main() {
  event_log_keeps_newest_and_escapes();
  settings_round_trip();
  settings_tolerate_junk();
  settings_file_io();
  http_parsing();
  private_addresses_only();
  forms_and_json();
  base64();
  return finish();
}
