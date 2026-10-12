// The last few things that happened to the signal, for the diagnostics page (R15): a
// volunteer at the venue reads "input lost 12 s ago" on the phone instead of needing
// someone with a shell. Every event is also printed, so the journal keeps the full
// history.
#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>

namespace ht {

class EventLog {
 public:
  using Clock = std::chrono::steady_clock;
  explicit EventLog(size_t capacity = 30, Clock::time_point start = Clock::now())
      : capacity_(capacity), start_(start) {}

  // Records and prints one line (printf-style).
  void add(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  void add_at(Clock::time_point when, std::string text);

  // [{"t":<seconds since start>,"text":"..."}, ...], oldest first.
  std::string json() const;
  size_t size() const { return events_.size(); }
  long long seconds_since_start(Clock::time_point when) const;

 private:
  struct Event {
    long long t;
    std::string text;
  };
  size_t capacity_;
  Clock::time_point start_;
  std::deque<Event> events_;
};

}  // namespace ht
