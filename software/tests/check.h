// Minimal test helper: CHECK records a failure and keeps going; finish() reports.
#pragma once

#include <cstdio>

inline int g_failures = 0;

#define CHECK(cond)                                                                 \
  do {                                                                              \
    if (!(cond)) {                                                                  \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                                 \
    }                                                                               \
  } while (0)

inline int finish() {
  if (g_failures) {
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
  }
  std::puts("all tests passed");
  return 0;
}
