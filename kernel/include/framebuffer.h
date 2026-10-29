#pragma once
#include <stdint.h>
#include <stddef.h>

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

/* Optional GPU backend initialization hook. Returns 0 on success. */
int gpu_init(void);
void fb_clear(uint32_t color);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);

/* Double buffering helpers. */
void *fb_back_buffer(void);
void *fb_front_buffer(void);
void fb_flip(void);
void fb_clear_back(uint32_t color);

/* Geometry helpers. */
uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t fb_stride(void);
