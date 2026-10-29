#pragma once

#include <stdint.h>

/* Copies the multiboot kernel command line. It must run before the physical
   memory manager hands out pages, because the bootloader does not reserve
   the string. */
void cmdline_init(uintptr_t multiboot_info_addr);

/* Returns 1 if the command line contains `name` as a whole space-separated
   word, else 0. */
int cmdline_has_flag(const char *name);
