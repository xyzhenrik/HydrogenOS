#include <stddef.h>
#include <stdint.h>

#include "apps/kernel_shell.h"
#include "console.h"
#include "core/demo_tasks.h"
#include "core/task.h"
#include "core/time.h"
#include "drivers/ata.h"
#include "drivers/block.h"
#include "framebuffer.h"
#include "fs/ramfs.h"
#include "fs/vfs.h"
#include "gdt.h"
#include "idt.h"
#include "io.h"
#include "isr.h"
#include "keyboard.h"
#include "kheap.h"
#include "mouse.h"
#include "multiboot.h"
#include "paging.h"
#include "pic.h"
#include "pit.h"
#include "pmm.h"
#include "serial.h"
#include "tty.h"
#include "video/fb_console.h"

static void initrd_setup(uintptr_t multiboot_info_addr) {
  multiboot_info_t *mbi = (multiboot_info_t *)(uintptr_t)multiboot_info_addr;
  if (!mbi) return;
  if (!(mbi->flags & MULTIBOOT_FLAG_MODS) || mbi->mods_count == 0) return;
  multiboot_module_t *mods =
      (multiboot_module_t *)(uintptr_t)mbi->mods_addr;
  if (mods[0].mod_end > mods[0].mod_start) {
    ramfs_set_cpio((const void *)(uintptr_t)mods[0].mod_start,
                   mods[0].mod_end - mods[0].mod_start);
  }
}

void kmain(uint32_t magic, uint32_t multiboot_info_addr) {
  (void)magic;

  serial_init();
  console_init(0x0B); // cyan on black
  console_write("HydrogenOS bootet...\n");
  console_write("A\n");

  // gdt_init(); // nutzen GRUB-GDT, IDT nimmt aktuellen CS
  console_write("B\n");
  idt_install();
  console_write("C\n");
  isr_install();
  console_write("D\n");
  pmm_init((uintptr_t)multiboot_info_addr);
  console_write("E\n");
  paging_init();
  console_write("F\n");
  int fb_ok = fb_init((uintptr_t)multiboot_info_addr);
  console_write("G\n");

  // IRQs bleiben vorerst maskiert; Shell pollt Tastatur selbst
  // IRQs aktivieren: Timer + Keyboard + Cascade, Rest maskiert
  pic_remap(0x20, 0x28);
  pit_init(100);
  time_init(100);
  outb(0x21, 0xF8); // IRQ0, IRQ1, IRQ2 frei
  outb(0xA1, 0xFF);

  // Einfacher KHeap: größer für Backbuffer (4 MiB)
  uintptr_t first_frame = pmm_first_free_frame();
  uintptr_t heap_phys = first_frame * 4096;
  int heap_pages = 1024; // 4 MiB
  for (int i = 0; i < heap_pages; ++i) {
    pmm_alloc_page();
  }
  kheap_init(heap_phys, (size_t)heap_pages * 4096);

  keyboard_init();
  console_write("H\n");
  mouse_init();
  ata_init();
  block_init();
  block_cache_init();
  initrd_setup((uintptr_t)multiboot_info_addr);
  vfs_init();
  if (fb_ok == 0) {
    mouse_set_bounds((int)framebuffer.width, (int)framebuffer.height);
  } else {
    // VGA text mode fallback: 80x25 cells, 8x16 font
    mouse_set_bounds(80 * 8, 25 * 16);
  }
  tty_init();
  console_write("I\n");
  int use_fb = (fb_ok == 0);
  if (use_fb) {
    fb_clear(0x00101010);
    fb_console_clear();
    fb_console_write("HydrogenOS Framebuffer Console\n");
  } else {
    console_clear();
    console_write("HydrogenOS Shell\n");
  }
  console_write("J\n");
  tasking_init();
  demo_tasks_start();
  sti();
  kernel_shell_run();
}
