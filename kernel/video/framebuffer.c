#include "framebuffer.h"
#include "fb_console.h"
#include "kheap.h"
#include "multiboot.h"
#include "paging.h"
#include "../drivers/virtio_gpu.h"
#include "../drivers/bochs_display.h"

#include <stddef.h>

/* Allow this file to build/link even when the PCI display drivers are not
   part of the kernel image (i386). When a driver is linked, its strong
   definitions override these weak declarations. */
void virtio_gpu_init(void) __attribute__((weak));
void *virtio_gpu_framebuffer(void) __attribute__((weak));
uint32_t virtio_gpu_width(void) __attribute__((weak));
uint32_t virtio_gpu_height(void) __attribute__((weak));
uint32_t virtio_gpu_stride(void) __attribute__((weak));
int bochs_display_init(BochsDisplayMode *out) __attribute__((weak));

fb_t framebuffer = {0};

static void *back_buffer = NULL;
static void *front_buffer = NULL;

static void *ensure_back_buffer(void) {
  if (back_buffer)
    return back_buffer;
  if (!framebuffer.addr)
    return NULL;
  size_t size = (size_t)framebuffer.pitch * (size_t)framebuffer.height;
  if (size == 0)
    return NULL;
  back_buffer = kalloc(size);
  return back_buffer;
}

static int try_virtio_gpu(void) {
  if (!virtio_gpu_init || !virtio_gpu_framebuffer)
    return -1;

  virtio_gpu_init();

  void *fb = virtio_gpu_framebuffer();
  if (!fb)
    return -1;

  framebuffer.addr = (uint8_t *)fb;
  framebuffer.pitch = virtio_gpu_stride ? virtio_gpu_stride() : 0;
  framebuffer.width = virtio_gpu_width ? virtio_gpu_width() : 0;
  framebuffer.height = virtio_gpu_height ? virtio_gpu_height() : 0;
  framebuffer.bpp = 32;

  if (framebuffer.pitch == 0 || framebuffer.width == 0 ||
      framebuffer.height == 0) {
    framebuffer.addr = NULL;
    return -1;
  }
  return 0;
}

static int try_bochs_display(void) {
  if (!bochs_display_init)
    return -1;

  BochsDisplayMode mode;
  if (bochs_display_init(&mode) != 0)
    return -1;

  framebuffer.addr = mode.base;
  framebuffer.pitch = mode.pitch;
  framebuffer.width = mode.width;
  framebuffer.height = mode.height;
  framebuffer.bpp = (uint8_t)mode.bpp;
  return 0;
}

int gpu_init(void) {
  if (try_virtio_gpu() == 0)
    return 0;
  return try_bochs_display();
}

static int try_multiboot(uintptr_t multiboot_info_addr) {
  const multiboot_info_t *mbi =
      (const multiboot_info_t *)multiboot_info_addr;
  if (!mbi || !(mbi->flags & MULTIBOOT_FLAG_FRAMEBUFFER))
    return -1;
  if (mbi->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB ||
      mbi->framebuffer_bpp != 32)
    return -1;

  uint64_t fb_addr = mbi->framebuffer_addr;
  size_t fb_size = (size_t)mbi->framebuffer_pitch * mbi->framebuffer_height;
  if (fb_size == 0)
    return -1;
  paging_map_region((uintptr_t)fb_addr, (uintptr_t)fb_addr, fb_size, PAGE_RW);

  framebuffer.addr = (uint8_t *)(uintptr_t)fb_addr;
  framebuffer.pitch = mbi->framebuffer_pitch;
  framebuffer.width = mbi->framebuffer_width;
  framebuffer.height = mbi->framebuffer_height;
  framebuffer.bpp = mbi->framebuffer_bpp;
  return 0;
}

/* Backend priority: a firmware/bootloader-provided VBE framebuffer is already
   live and needs no driver, so it wins; otherwise a native PCI display driver
   sets a mode. PCI enumeration must have completed before this runs. */
int fb_init(uintptr_t multiboot_info_addr) {
  if (back_buffer) {
    kfree(back_buffer);
    back_buffer = NULL;
  }
  front_buffer = NULL;

  framebuffer.addr = NULL;
  framebuffer.pitch = 0;
  framebuffer.width = 0;
  framebuffer.height = 0;
  framebuffer.bpp = 0;

  if (try_multiboot(multiboot_info_addr) != 0 && gpu_init() != 0)
    return -1;

  if (fb_console_init() != 0) {
    framebuffer.addr = NULL;
    return -3;
  }
  front_buffer = framebuffer.addr;
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

void *fb_back_buffer(void) {
  return ensure_back_buffer();
}

void *fb_front_buffer(void) {
  if (!front_buffer && framebuffer.addr)
    front_buffer = framebuffer.addr;
  return front_buffer;
}

void fb_flip(void) {
  void *back = ensure_back_buffer();
  void *front = fb_front_buffer();
  if (!back || !front)
    return;
  size_t size = (size_t)framebuffer.pitch * (size_t)framebuffer.height;
  uint8_t *dst = (uint8_t *)front;
  const uint8_t *src = (const uint8_t *)back;
  for (size_t i = 0; i < size; ++i)
    dst[i] = src[i];
}

void fb_clear_back(uint32_t color) {
  void *back = ensure_back_buffer();
  if (!back)
    return;
  size_t pixels = ((size_t)framebuffer.pitch * (size_t)framebuffer.height) / 4;
  uint32_t *buf = (uint32_t *)back;
  for (size_t i = 0; i < pixels; ++i)
    buf[i] = color;
}

uint32_t fb_width(void) {
  return framebuffer.width;
}

uint32_t fb_height(void) {
  return framebuffer.height;
}

uint32_t fb_stride(void) {
  return framebuffer.pitch;
}
