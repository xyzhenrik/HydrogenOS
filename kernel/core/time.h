#pragma once

#include <stdint.h>

void time_init(uint32_t tick_hz);
void time_on_tick(void);
uint64_t time_ms(void);
