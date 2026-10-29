#include <stdint.h>

#include "console.h"
#include "core/time.h"
#include "drivers/ata.h"
#include "drivers/pci.h"
#include "drivers/block.h"
#include "elf64.h"
#include "elf_loader.h"
#include "framebuffer.h"
#include "fs/vfs.h"
#include "gdt.h"
#include "higher_half.h"
#include "idt.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/acpi.h"
#include "io.h"
#include "isr.h"
#include "keyboard.h"
#include "kheap.h"
#include "mouse.h"
#include "multiboot.h"
#include "paging.h"
#include "pic.h"
#include "pit.h"
#include "rtc.h"
#include "pmm.h"
#include "process.h"
#include "spawn_args.h"
#include "user_exec.h"
#include "serial.h"
#include "syscall_arch.h"
#include "syscall.h"
#include "tty.h"
#include "video/fb_console.h"
#include "fs/ramfs.h"
#include "net/net.h"

extern uint8_t stack_top64;

static void shell_loop(void);

static void initrd_setup(uintptr_t multiboot_info_addr) {
  multiboot_info_t *mbi = (multiboot_info_t *)(uintptr_t)multiboot_info_addr;
  if (!mbi) return;
  if (!(mbi->flags & MULTIBOOT_FLAG_MODS) || mbi->mods_count == 0) return;
  multiboot_module_t *mods =
      (multiboot_module_t *)(uintptr_t)mbi->mods_addr;
  if (mods[0].mod_end > mods[0].mod_start) {
    uintptr_t phys = (uintptr_t)mods[0].mod_start;
    uint32_t size = (uint32_t)(mods[0].mod_end - mods[0].mod_start);
    const void *ptr = (const void *)phys;
    if (phys >= 0x40000000u) {
      uintptr_t virt = (uintptr_t)KERNEL_VMA_BASE + phys;
      paging_map_region(phys, virt, size, PAGE_RW);
      ptr = (const void *)virt;
    }
    ramfs_set_cpio(ptr, size);
  }
}

static int use_fb(void) {
  return framebuffer.addr != NULL;
}

static void kputs(const char *s) {
  if (use_fb())
    fb_console_write(s);
  else
    console_write(s);
}

static void write_u64_hex(uint64_t value) {
  const char *digits = "0123456789ABCDEF";
  if (use_fb())
    fb_console_write("0x");
  else
    console_write("0x");
  for (int i = 15; i >= 0; --i) {
    uint8_t nibble = (value >> (i * 4)) & 0xF;
    if (use_fb())
      fb_console_putc(digits[nibble]);
    else
      console_putc(digits[nibble]);
  }
}

static void write_u64_dec(uint64_t value) {
  char buf[21];
  int i = 0;
  if (value == 0) {
    if (use_fb())
      fb_console_putc('0');
    else
      console_putc('0');
    return;
  }
  while (value > 0 && i < (int)sizeof(buf)) {
    buf[i++] = (char)('0' + (value % 10));
    value /= 10;
  }
  while (i > 0) {
    if (use_fb())
      fb_console_putc(buf[--i]);
    else
      console_putc(buf[--i]);
  }
}

static int str_eq(const char *a, const char *b) {
  while (*a && *b) {
    if (*a != *b)
      return 0;
    ++a;
    ++b;
  }
  return (*a == '\0' && *b == '\0');
}

static void cmd_help(void) {
  kputs("Commands: help, uptime, mem, clear, elfinfo, elfload, user, echo, cat, time, sleep\n");
}

static void cmd_uptime(void) {
  kputs("Uptime(ms): ");
  write_u64_dec(time_ms());
  kputs("\n");
}

static void cmd_mem(void) {
  uintptr_t frame = pmm_first_free_frame();
  kputs("First free frame: ");
  write_u64_hex((uint64_t)frame);
  kputs(" addr=");
  write_u64_hex((uint64_t)frame * 4096);
  kputs("\n");
}

static void cmd_clear(void) {
  if (use_fb())
    fb_console_clear();
  else
    console_clear();
}

static void cmd_elfinfo(void) {
  kputs("ELF64 loader stub: ready\n");
}

static void cmd_elfload(void) {
  int rc = elf64_load_image((const void *)0x0, 0);
  if (use_fb()) {
    fb_console_write("ELF load rc=");
    if (rc == 0)
      fb_console_write("0\n");
    else
      fb_console_write("-1\n");
  } else {
    console_write("ELF load rc=");
    if (rc == 0)
      console_write("0\n");
    else
      console_write("-1\n");
  }
}

extern void syscall_set_bootstrap(process_t *proc);

static void user_return_trampoline(uint64_t status) {
  process_t *exited = process_current();
  __asm__ volatile("mov $0x10, %%ax\n"
                   "mov %%ax, %%ds\n"
                   "mov %%ax, %%es\n"
                   "mov %%ax, %%fs\n"
                   "mov %%ax, %%gs\n"
                   :
                   :
                   : "rax");
  process_mark_exited(exited, (int)status);
  process_set_current(process_kernel());
  paging_switch(paging_kernel_pml4());
  process_destroy(exited);
  kputs("user exited with status ");
  write_u64_dec(status);
  kputs("\n");
  shell_loop();
}

static void enter_usermode(uintptr_t entry, uintptr_t user_stack_top) {
  __asm__ volatile(
      "mov $0x1B, %%ax\n"
      "mov %%ax, %%ds\n"
      "mov %%ax, %%es\n"
      "mov %%ax, %%fs\n"
      "mov %%ax, %%gs\n"
      "pushq $0x1B\n"
      "pushq %[user_rsp]\n"
      "pushfq\n"
      "pop %%rax\n"
      "or $0x200, %%rax\n"
      "pushq %%rax\n"
      "pushq $0x23\n"
      "pushq %[entry]\n"
      "iretq\n"
      :
      : [user_rsp] "r"(user_stack_top), [entry] "r"(entry)
      : "rax", "memory");
}

static process_t *load_user_process(const uint8_t *elf, size_t elf_size,
                                    const char *name) {
  if (elf64_validate(elf, elf_size) != 0) {
    kputs("user: invalid ELF\n");
    return 0;
  }

  process_t *prev_proc = process_current();
  uint64_t *prev_pml4 = paging_current_pml4();
  uint64_t *user_pml4 = paging_create_user_pml4();
  if (!user_pml4) {
    kputs("user: no pml4\n");
    return 0;
  }
  process_t *proc = process_create(user_pml4, prev_proc ? prev_proc->pid : 0);
  if (!proc) {
    kputs("user: no proc\n");
    paging_destroy_user_pml4(user_pml4);
    return 0;
  }
  process_set_name(proc, name);
  paging_switch(user_pml4);

  if (elf64_load_image(elf, elf_size) != 0) {
    kputs("user: load failed\n");
    paging_switch(prev_pml4);
    process_destroy(proc);
    return 0;
  }

  const uintptr_t user_stack = 0x0000000080100000;
  const size_t stack_pages = 16;
  /* A PROT_NONE page below the stack makes an overflow fault instead of
   * running into whatever lies beneath, so the image must not reach it. */
  for (size_t i = 0; i <= stack_pages; ++i) {
    if (paging_is_mapped(user_stack - 0x1000 + i * 0x1000)) {
      kputs("user: image overlaps stack guard\n");
      paging_switch(prev_pml4);
      process_destroy(proc);
      return 0;
    }
  }
  uintptr_t guard_phys = pmm_alloc_page();
  if (!guard_phys ||
      paging_user_map_page(user_pml4, user_stack - 0x1000, guard_phys,
                           PAGE_PROT_NONE) != 0) {
    if (guard_phys)
      pmm_free_page(guard_phys);
    kputs("user: no memory for stack guard\n");
    paging_switch(prev_pml4);
    process_destroy(proc);
    return 0;
  }
  for (size_t i = 0; i < stack_pages; ++i) {
    uintptr_t phys = pmm_alloc_page();
    if (!phys) {
      kputs("user: no memory for stack\n");
      paging_switch(prev_pml4);
      process_destroy(proc);
      return 0;
    }
    paging_map_region(phys, user_stack + i * 0x1000, 0x1000,
                      PAGE_USER | PAGE_RW);
    uint8_t *page = (uint8_t *)(uintptr_t)phys;
    for (size_t j = 0; j < 4096; ++j)
      page[j] = 0;
  }
  paging_user_protect_range(paging_current_pml4(), user_stack,
                            stack_pages * 0x1000, PAGE_RW | PAGE_NO_EXEC);
  const elf64_ehdr_t *eh = (const elf64_ehdr_t *)elf;
  regs_t initial = {0};
  initial.rip = (uint64_t)(uintptr_t)eh->e_entry;
  initial.cs = GDT_USER_CODE_SELECTOR;
  initial.rflags = 0x202;
  initial.rsp = user_stack + stack_pages * 0x1000 - 16;
  initial.ss = GDT_USER_DATA_SELECTOR;
  process_set_frame(proc, &initial);
  paging_switch(prev_pml4);
  return proc;
}

static int run_user_elf(const uint8_t *elf, size_t elf_size, const char *name) {
  process_t *proc = load_user_process(elf, elf_size, name);
  if (!proc) {
    return -1;
  }
  kputs("Entering user mode (ELF + sys_write)...\n");
  process_set_current(proc);
  syscall_set_bootstrap(proc);
  paging_switch(proc->pml4);
  syscall_set_user_return((uintptr_t)user_return_trampoline,
                          (uintptr_t)&stack_top64 - 16);
  enter_usermode(proc->frame.rip, proc->frame.rsp);
  return 0;
}

static int run_user_from_vfs(const char *path) {
  if (!vfs_is_ready())
    return -1;
  vfs_stat_t st;
  if (vfs_stat(path, &st) != 0 || st.type != 1 || st.size == 0)
    return -1;
  uint8_t *buf = (uint8_t *)kalloc(st.size);
  if (!buf)
    return -1;
  uint32_t got = 0;
  if (vfs_read_file(path, buf, st.size, &got) != 0 || got == 0) {
    kfree(buf);
    return -1;
  }
  int rc = run_user_elf(buf, got, path);
  if (rc != 0)
    kfree(buf);
  return rc;
}

static int spawn_from_vfs(const char *path, const spawn_args_t *args,
                          int bind_stdio, int stdin_fd, int stdout_fd,
                          int stderr_fd, uint64_t *out_pid) {
  process_t *parent = process_current();
  if (out_pid)
    *out_pid = 0;
  if (!path || !vfs_is_ready())
    return USER_SPAWN_ERR_EXEC;
  vfs_stat_t st;
  if (vfs_stat(path, &st) != 0 || st.type != 1 || st.size == 0)
    return USER_SPAWN_ERR_EXEC;
  uint8_t *buf = (uint8_t *)kalloc(st.size);
  if (!buf)
    return USER_SPAWN_ERR_EXEC;
  uint32_t got = 0;
  if (vfs_read_file(path, buf, st.size, &got) != 0 || got == 0) {
    kfree(buf);
    return USER_SPAWN_ERR_EXEC;
  }
  process_t *proc = load_user_process(buf, got, path);
  kfree(buf);
  if (!proc)
    return USER_SPAWN_ERR_EXEC;
  if (args && spawn_args_apply(proc, args) != 0) {
    process_destroy(proc);
    return USER_SPAWN_ERR_EXEC;
  }
  if (parent && parent != process_kernel()) {
    if (process_inherit_handles(proc, parent) != 0 ||
        process_inherit_stdio(proc, parent) != 0) {
      process_destroy(proc);
      return USER_SPAWN_ERR_EXEC;
    }
    if (bind_stdio &&
        (process_bind_stdio_from_fd(proc, 0, parent, stdin_fd) != 0 ||
         process_bind_stdio_from_fd(proc, 1, parent, stdout_fd) != 0 ||
         process_bind_stdio_from_fd(proc, 2, parent, stderr_fd) != 0)) {
      process_destroy(proc);
      return USER_SPAWN_ERR_STDIO;
    }
  }
  if (out_pid)
    *out_pid = proc->pid;
  return 0;
}

int user_spawn_from_vfs(const char *path, regs_t *parent_regs, uint64_t *out_pid) {
  (void)parent_regs;
  return spawn_from_vfs(path, 0, 0, PROCESS_STDIO_BIND_INHERIT,
                        PROCESS_STDIO_BIND_INHERIT, PROCESS_STDIO_BIND_INHERIT,
                        out_pid);
}

int user_spawn_with_stdio_from_vfs(const char *path, regs_t *parent_regs,
                                   uint64_t *out_pid, int stdin_fd,
                                   int stdout_fd, int stderr_fd) {
  (void)parent_regs;
  return spawn_from_vfs(path, 0, 1, stdin_fd, stdout_fd, stderr_fd, out_pid);
}

int user_spawn_args_from_vfs(const char *path, const spawn_args_t *args,
                             uint64_t *out_pid, int stdin_fd, int stdout_fd,
                             int stderr_fd) {
  return spawn_from_vfs(path, args, 1, stdin_fd, stdout_fd, stderr_fd, out_pid);
}

static void cmd_user(void) {
  extern const uint8_t _binary_build_x86_64_user_hello_elf_start[];
  extern const uint8_t _binary_build_x86_64_user_hello_elf_end[];

  const uint8_t *elf = _binary_build_x86_64_user_hello_elf_start;
  size_t elf_size = (size_t)(_binary_build_x86_64_user_hello_elf_end - elf);
  run_user_elf(elf, elf_size, "hello");
}

static void cmd_echo(void) {
  extern const uint8_t _binary_build_x86_64_user_echo_elf_start[];
  extern const uint8_t _binary_build_x86_64_user_echo_elf_end[];

  const uint8_t *elf = _binary_build_x86_64_user_echo_elf_start;
  size_t elf_size = (size_t)(_binary_build_x86_64_user_echo_elf_end - elf);
  run_user_elf(elf, elf_size, "echo");
}

static void cmd_cat(void) {
  extern const uint8_t _binary_build_x86_64_user_cat_elf_start[];
  extern const uint8_t _binary_build_x86_64_user_cat_elf_end[];

  const uint8_t *elf = _binary_build_x86_64_user_cat_elf_start;
  size_t elf_size = (size_t)(_binary_build_x86_64_user_cat_elf_end - elf);
  run_user_elf(elf, elf_size, "cat");
}

static void cmd_time(void) {
  extern const uint8_t _binary_build_x86_64_user_time_elf_start[];
  extern const uint8_t _binary_build_x86_64_user_time_elf_end[];

  const uint8_t *elf = _binary_build_x86_64_user_time_elf_start;
  size_t elf_size = (size_t)(_binary_build_x86_64_user_time_elf_end - elf);
  run_user_elf(elf, elf_size, "time");
}

static void cmd_sleep(void) {
  extern const uint8_t _binary_build_x86_64_user_sleep_elf_start[];
  extern const uint8_t _binary_build_x86_64_user_sleep_elf_end[];

  const uint8_t *elf = _binary_build_x86_64_user_sleep_elf_start;
  size_t elf_size = (size_t)(_binary_build_x86_64_user_sleep_elf_end - elf);
  run_user_elf(elf, elf_size, "sleep");
}

static void shell_loop(void) {
  char line[128];
  size_t len = 0;
  kputs("> ");
  for (;;) {
    int c = keyboard_read_char();
    if (c < 0) {
      __asm__ volatile("hlt");
      continue;
    }
    if (c == '\b') {
      if (len > 0) {
        len--;
        if (use_fb())
          fb_console_putc('\b');
        else
          console_putc('\b');
      }
      continue;
    }
    if (c == '\n') {
      if (use_fb())
        fb_console_putc('\n');
      else
        console_putc('\n');
      line[len] = '\0';
      if (len == 0) {
        kputs("> ");
        continue;
      }
      if (str_eq(line, "help")) {
        cmd_help();
      } else if (str_eq(line, "uptime")) {
        cmd_uptime();
      } else if (str_eq(line, "mem")) {
        cmd_mem();
      } else if (str_eq(line, "clear")) {
        cmd_clear();
      } else if (str_eq(line, "elfinfo")) {
        cmd_elfinfo();
      } else if (str_eq(line, "elfload")) {
        cmd_elfload();
      } else if (str_eq(line, "user")) {
        cmd_user();
      } else if (str_eq(line, "echo")) {
        cmd_echo();
      } else if (str_eq(line, "cat")) {
        cmd_cat();
      } else if (str_eq(line, "time")) {
        cmd_time();
      } else if (str_eq(line, "sleep")) {
        cmd_sleep();
      } else {
        kputs("Unknown command. Type 'help'.\n");
      }
      len = 0;
      kputs("> ");
      continue;
    }
    if (c >= 0x20 && c < 0x100) {
      if (len + 1 < sizeof(line)) {
        line[len++] = (char)c;
        if (use_fb())
          fb_console_putc((char)c);
        else
          console_putc((char)c);
      }
    }
  }
}

void kmain64(uint64_t magic, uint64_t multiboot_info_addr) {
  (void)magic;
  (void)multiboot_info_addr;

  serial_init();
  console_init(0x0B); // cyan on black
  console_write("HydrogenOS x86_64 bootet...\n");

  pmm_init((uintptr_t)multiboot_info_addr);

  int heap_pages = 8192; // 32 MiB (framebuffer console at 1080p/2K)
  uintptr_t heap_phys = pmm_alloc_contiguous((size_t)heap_pages);
  if (!heap_phys) {
    kputs("heap alloc failed\n");
    for (;;)
      __asm__ volatile("cli\n\thlt");
  }
  kheap_init(heap_phys, (size_t)heap_pages * 4096);

  paging_init();
  process_init(paging_kernel_pml4());

  /* Display drivers probe the PCI registry, so enumerate before fb_init. */
  pci_init();
  pci_dump_devices();
  int fb_ok = fb_init((uintptr_t)multiboot_info_addr);
  if (fb_ok == 0) {
    fb_clear(0x00101010);
    fb_console_clear();
  }

  idt_install();
  isr_install();
  lapic_init();  gdt_init();
  syscall_arch_init();
  pic_remap(0x20, 0x28);
  pit_init(100);
  time_init(100);
  acpi_init();
  if (rtc_init() != 0)
    serial_write("[RTC] CMOS clock unreadable, wall time unavailable\n");
  outb(0x21, 0xF8); // IRQ0, IRQ1, IRQ2 frei
  outb(0xA1, 0xEF); // IRQ12 frei
  keyboard_init();
  mouse_init();
  ata_init();
  block_init();
  block_cache_init();
  net_init();
  initrd_setup((uintptr_t)multiboot_info_addr);
  vfs_init();
  serial_write("[VFS] root: ");
  serial_write(vfs_root_name());
  serial_write("\n");
  if (fb_ok == 0) {
    mouse_set_bounds((int)framebuffer.width, (int)framebuffer.height);
  } else {
    mouse_set_bounds(80 * 8, 25 * 16);
  }
  tty_init();
  sti();
  if (run_user_from_vfs("/init") != 0) {
    kputs("userspace init missing, trying direct userspace shell fallback\n");
    if (run_user_from_vfs("/shell") == 0) {
      return;
    }
    kputs("userspace shell missing, falling back to kernel shell\n");
    shell_loop();
  }
}








