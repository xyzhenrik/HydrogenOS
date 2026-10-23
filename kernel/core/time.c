#include "time.h"

static volatile uint64_t uptime_ms = 0;
static uint32_t tick_hz = 100;
static uint32_t tick_ms = 10;
static uint32_t tick_rem = 0;

void time_init(uint32_t hz) {
  tick_hz = hz;
  uptime_ms = 0;
  tick_ms = 1000 / tick_hz;
  tick_rem = 1000 % tick_hz;
}

void time_on_tick(void) {
  uptime_ms += tick_ms;
  tick_rem += (1000 % tick_hz);
  if (tick_rem >= tick_hz) {
    uptime_ms++;
    tick_rem -= tick_hz;
  }
}

uint64_t time_ms(void) { return uptime_ms; }
