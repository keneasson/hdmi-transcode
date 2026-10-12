#include "event_log.h"

#include <cstdarg>
#include <cstdio>

#include "http_server.h"

namespace ht {

void EventLog::add(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  std::printf("%s\n", buf);
  add_at(Clock::now(), buf);
}

void EventLog::add_at(Clock::time_point when, std::string text) {
  if (capacity_ == 0) return;
  if (events_.size() == capacity_) events_.pop_front();
  events_.push_back({seconds_since_start(when), std::move(text)});
}

long long EventLog::seconds_since_start(Clock::time_point when) const {
  return std::chrono::duration_cast<std::chrono::seconds>(when - start_).count();
}

std::string EventLog::json() const {
  std::string s = "[";
  for (const auto& e : events_) {
    if (s.size() > 1) s += ',';
    s += "{\"t\":" + std::to_string(e.t) + ",\"text\":\"" + json_escape(e.text) + "\"}";
  }
  return s + "]";
}

}  // namespace ht
