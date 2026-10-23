#pragma once
#include <stdint.h>

typedef struct {
  uint8_t *addr; // identity-mapped pointer
  uint32_t pitch;
  uint32_t width;
  uint32_t height;
  uint8_t bpp;
} fb_t;

typedef struct {
  uint32_t pitch;
  uint32_t width;
  uint32_t height;
  uint8_t bpp;
  uint8_t type; // 1=text, 2=RGB
  uint8_t reserved[2];
  uint32_t addr_low;
  uint32_t addr_high;
} fb_info_t;

extern fb_t framebuffer;
int fb_init(uintptr_t multiboot_info_addr);
void fb_clear(uint32_t color);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
