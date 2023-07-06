#include "profiler.h"

static BOOL performance_frequency_initialized = FALSE;
static ULONGLONG performance_frequency;
static double ticks_per_millisecond;

PROFILETOKEN ProfileStart(void) { return KeQueryPerformanceCounter(); }

double ProfileStop(PROFILETOKEN *start_token) {
  PROFILETOKEN now = KeQueryPerformanceCounter();

  if (!performance_frequency_initialized) {
    performance_frequency = KeQueryPerformanceFrequency();
    ticks_per_millisecond = (double)performance_frequency / 1000.0;
    performance_frequency_initialized = TRUE;
  }

  PROFILETOKEN delta = now - *start_token;
  return (double)delta / ticks_per_millisecond;
}
