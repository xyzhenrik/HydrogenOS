#include "framebuffer.h"
#include "fb_console.h"
#include "kheap.h"
#include "multiboot.h"
#include "paging.h"

fb_t framebuffer = {0};

int fb_init(uintptr_t multiboot_info_addr) {
  framebuffer.addr = NULL;
  multiboot_info_t *mbi = (multiboot_info_t *)(uintptr_t)multiboot_info_addr;
  if (!(mbi->flags & (1 << 12))) return -1; // no framebuffer info
  uint64_t fb_addr = mbi->framebuffer_addr;
  framebuffer.addr = (uint8_t *)(uintptr_t)fb_addr;
  framebuffer.pitch = mbi->framebuffer_pitch;
  framebuffer.width = mbi->framebuffer_width;
  framebuffer.height = mbi->framebuffer_height;
  framebuffer.bpp = mbi->framebuffer_bpp;
  if (framebuffer.bpp != 32) {
    framebuffer.addr = NULL;
    return -2;
  }
  size_t fb_size = (size_t)framebuffer.pitch * (size_t)framebuffer.height;
  paging_map_region((uintptr_t)fb_addr, (uintptr_t)fb_addr, fb_size, PAGE_RW);
  if (fb_console_init() != 0) {
    framebuffer.addr = NULL;
    return -3;
  }
  return 0;
}

void fb_clear(uint32_t color) {
  if (!framebuffer.addr)
    return;
  for (uint32_t y = 0; y < framebuffer.height; ++y) {
    uint32_t *row = (uint32_t *)(framebuffer.addr + y * framebuffer.pitch);
    for (uint32_t x = 0; x < framebuffer.width; ++x) {
      row[x] = color;
    }
  }
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
  if (!framebuffer.addr)
    return;
  if (x >= framebuffer.width || y >= framebuffer.height)
    return;
  uint32_t *row = (uint32_t *)(framebuffer.addr + y * framebuffer.pitch);
  row[x] = color;
}
