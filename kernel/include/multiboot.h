#pragma once

#include <stdint.h>

#define MULTIBOOT_MAGIC 0x2BADB002
#define MULTIBOOT_FLAG_MMAP (1 << 6)
#define MULTIBOOT_FLAG_MODS (1 << 3)
#define MULTIBOOT_FLAG_FRAMEBUFFER (1 << 12)
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB 1

typedef struct multiboot_mmap_entry {
  uint32_t size;
  uint64_t addr;
  uint64_t len;
  uint32_t type;
} __attribute__((packed)) multiboot_mmap_entry_t;

typedef struct multiboot_info {
  uint32_t flags;
  uint32_t mem_lower;
  uint32_t mem_upper;
  uint32_t boot_device;
  uint32_t cmdline;
  uint32_t mods_count;
  uint32_t mods_addr;
  uint32_t syms[4];
  uint32_t mmap_length;
  uint32_t mmap_addr;
  uint32_t drives_length;
  uint32_t drives_addr;
  uint32_t config_table;
  uint32_t boot_loader_name;
  uint32_t apm_table;
  uint32_t vbe_control_info;
  uint32_t vbe_mode_info;
  uint16_t vbe_mode;
  uint16_t vbe_interface_seg;
  uint16_t vbe_interface_off;
  uint16_t vbe_interface_len;
  uint64_t framebuffer_addr;
  uint32_t framebuffer_pitch;
  uint32_t framebuffer_width;
  uint32_t framebuffer_height;
  uint8_t framebuffer_bpp;
  uint8_t framebuffer_type;
  uint8_t color_info[6];
} multiboot_info_t;

typedef struct multiboot_module {
  uint32_t mod_start;
  uint32_t mod_end;
  uint32_t string;
  uint32_t reserved;
} multiboot_module_t;

enum {
  MULTIBOOT_MEMORY_AVAILABLE = 1,
};
