#include "kernel_shell.h"
#include "console.h"
#include "core/demo_tasks.h"
#include "core/task.h"
#include "core/time.h"
#include "drivers/ata.h"
#include "drivers/block.h"
#include "drivers/mbr.h"
#include "framebuffer.h"
#include "fs/ext2.h"
#include "fs/vfs.h"
#include "keyboard.h"
#include "mouse.h"
#include "tty.h"
#include "video/fb_console.h"
#include <stddef.h>

static void putc_out(char c) { tty_putc(c); }
static void write_out(const char *s) { tty_write(s); }

static const char *prompt = "hydrogen$ ";
static void print_prompt(void) {
  write_out("\n");
  write_out(prompt);
}

// History (ring buffer)
#define HIST_SIZE 16
static char history[HIST_SIZE][128];
static int hist_count = 0; // number of stored entries (<= HIST_SIZE)
static int hist_head = 0;  // next insert position

static void history_push(const char *line) {
  if (!line || !line[0])
    return; // skip empty
  // avoid consecutive duplicates
  if (hist_count > 0) {
    int last_idx = (hist_head + HIST_SIZE - 1) % HIST_SIZE;
    const char *prev = history[last_idx];
    size_t i = 0;
    for (; line[i] && prev[i]; ++i) {
      if (line[i] != prev[i])
        break;
    }
    if (line[i] == 0 && prev[i] == 0)
      return;
  }
  size_t i = 0;
  for (; i < sizeof(history[0]) - 1 && line[i]; ++i)
    history[hist_head][i] = line[i];
  history[hist_head][i] = 0;
  hist_head = (hist_head + 1) % HIST_SIZE;
  if (hist_count < HIST_SIZE)
    hist_count++;
}

static int history_load(int pos_from_newest, char *out) {
  // 0 = latest, 1 = previous ...
  if (pos_from_newest < 0 || pos_from_newest >= hist_count)
    return -1;
  int idx = hist_head - 1 - pos_from_newest;
  if (idx < 0)
    idx += HIST_SIZE;
  size_t i = 0;
  for (; history[idx][i] && i < 127; ++i)
    out[i] = history[idx][i];
  out[i] = 0;
  return (int)i;
}

static int str_eq(const char *a, const char *b) {
  size_t i = 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i])
      return 0;
    i++;
  }
  return a[i] == b[i];
}

static int str_starts(const char *s, const char *p) {
  size_t i = 0;
  while (p[i]) {
    if (s[i] != p[i])
      return 0;
    i++;
  }
  return 1;
}

static void write_hex32(uint32_t v) {
  const char *d = "0123456789ABCDEF";
  for (int i = 7; i >= 0; --i) {
    putc_out(d[(v >> (i * 4)) & 0xF]);
  }
}

static void write_hex8(uint8_t v) {
  const char *d = "0123456789ABCDEF";
  putc_out(d[(v >> 4) & 0xF]);
  putc_out(d[v & 0xF]);
}

static int parse_uint(const char *s, uint32_t *out, const char **end) {
  while (*s == ' ')
    s++;
  int base = 10;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
    base = 16;
    s += 2;
  }
  uint32_t val = 0;
  int ok = 0;
  for (;;) {
    char c = *s;
    int digit = -1;
    if (c >= '0' && c <= '9')
      digit = c - '0';
    else if (base == 16 && c >= 'a' && c <= 'f')
      digit = 10 + (c - 'a');
    else if (base == 16 && c >= 'A' && c <= 'F')
      digit = 10 + (c - 'A');
    else
      break;
    val = (uint32_t)(val * (uint32_t)base + (uint32_t)digit);
    ok = 1;
    s++;
  }
  if (!ok)
    return -1;
  if (out)
    *out = val;
  if (end)
    *end = s;
  return 0;
}

static uint8_t read_buf[2048];
static uint8_t file_buf[4096];
static int last_fd = -1;

static void ls_cb(const char *name, uint8_t type, void *ctx) {
  (void)ctx;
  write_out("\n");
  if (type == 1)
    write_out("f ");
  else if (type == 2)
    write_out("d ");
  else
    write_out("? ");
  write_out(name);
}

void kernel_shell_run(void) {
  char line[128];
  size_t len = 0;
  size_t cursor = 0;
  size_t prev_len = 0;
  int hist_pos = -1; // -1 = not browsing history

  print_prompt();
  for (;;) {
    mouse_poll();
    keyboard_poll();
    int ch = keyboard_read_char();
    if (ch == -1) {
      if (framebuffer.addr == NULL) {
        int mx, my;
        if (mouse_get_pos(&mx, &my)) {
          uint32_t col = (uint32_t)(mx / 8);
          uint32_t row = (uint32_t)(my / 16);
          console_mouse_update(col, row);
        }
      }
      task_yield();
      continue;
    }

    if (ch == '\b') {
      if (cursor > 0) {
        for (size_t i = cursor - 1; i + 1 < len; ++i) {
          line[i] = line[i + 1];
        }
        len--;
        cursor--;
      }
    } else if (ch == KEY_LEFT) {
      if (cursor > 0)
        cursor--;
    } else if (ch == KEY_RIGHT) {
      if (cursor < len)
        cursor++;
    } else if (ch == KEY_UP) {
      if (hist_count > 0 && hist_pos + 1 < hist_count) {
        hist_pos++;
        int l = history_load(hist_pos, line);
        if (l < 0)
          l = 0;
        len = (size_t)l;
        cursor = len;
      }
    } else if (ch == KEY_DOWN) {
      if (hist_pos >= 0) {
        hist_pos--;
        if (hist_pos >= 0) {
          int l = history_load(hist_pos, line);
          if (l < 0)
            l = 0;
          len = (size_t)l;
          cursor = len;
        } else {
          len = 0;
          cursor = 0;
          line[0] = 0;
        }
      }
    } else if (ch == KEY_HOME) {
      cursor = 0;
    } else if (ch == KEY_END) {
      cursor = len;
    } else if (ch == KEY_DEL) {
      if (cursor < len) {
        for (size_t i = cursor; i + 1 < len; ++i)
          line[i] = line[i + 1];
        len--;
      }
    } else if (ch == '\n') {
      putc_out('\n');
      line[len] = 0;
      if (len > 0) {
        if (str_eq(line, "help")) {
          write_out("\nCommands: help, clear, ticks, tasks, mouse, scheduler, "
                    "ata, block, mbr, read, vfs, ext2, mount, umount, sync, ls, "
                    "cat, stat, open, fread, close");
        } else if (str_eq(line, "clear")) {
          tty_write("\n");
          // clear und Prompt neu
          // Hinweis: fb_console_clear/console_clear setzen Cursor auf 0,0
          if (framebuffer.addr)
            fb_console_clear();
          else
            console_clear();
        } else if (str_eq(line, "ticks")) {
          write_out("\nTicks(ms)=0x");
          uint64_t ms = time_ms();
          const char *d = "0123456789ABCDEF";
          for (int i = 60; i >= 0; i -= 4) {
            putc_out(d[(ms >> i) & 0xF]);
          }
        } else if (str_eq(line, "scheduler")) {
          write_out("\nScheduler: ");
          write_out(task_is_enabled() ? "on" : "off");
          write_out(" (yield=int 0x30)");
        } else if (str_eq(line, "scheduler on")) {
          task_set_enabled(1);
          write_out("\nScheduler: on");
        } else if (str_eq(line, "scheduler off")) {
          task_set_enabled(0);
          write_out("\nScheduler: off");
        } else if (str_eq(line, "tasks")) {
          uint32_t a = 0, b = 0;
          demo_tasks_snapshot(&a, &b);
          write_out("\nTasks A=0x");
          write_hex32(a);
          write_out(" B=0x");
          write_hex32(b);
        } else if (str_eq(line, "ata")) {
          const char *model = ata_model();
          if (!model || !model[0]) {
            write_out("\nATA: no disk");
          } else {
            write_out("\nATA: ");
            write_out(model);
          }
        } else if (str_eq(line, "block")) {
          const block_dev_t *dev = block_primary();
          if (!dev) {
            write_out("\nBlock: no device");
          } else {
            write_out("\nBlock: ");
            write_out(dev->name);
            write_out(" ");
            if (dev->model && dev->model[0])
              write_out(dev->model);
            write_out(" sectors=0x");
            write_hex32(dev->sector_count);
          }
        } else if (str_eq(line, "mbr")) {
          mbr_t m;
          if (mbr_read(&m) != 0) {
            write_out("\nmbr: read error");
          } else if (!mbr_valid(&m)) {
            write_out("\nmbr: invalid signature");
          } else {
            write_out("\nmbr: partitions");
            for (int i = 0; i < 4; ++i) {
              mbr_part_t *p = &m.parts[i];
              if (p->type == 0 || p->sectors == 0)
                continue;
              write_out("\n  ");
              putc_out('0' + i);
              write_out(": type=0x");
              write_hex8(p->type);
              write_out(" lba=0x");
              write_hex32(p->lba_start);
              write_out(" sectors=0x");
              write_hex32(p->sectors);
            }
          }
        } else if (str_eq(line, "ext2")) {
          if (ext2_is_mounted() || ext2_mount_auto() == 0) {
            ext2_info_t info;
            if (ext2_info(&info) == 0) {
              write_out("\next2: base_lba=0x");
              write_hex32(info.base_lba);
              write_out(" block=0x");
              write_hex32(info.block_size);
              write_out(" inode=0x");
              write_hex32(info.inode_size);
              write_out(" blocks=0x");
              write_hex32(info.blocks);
              write_out(" inodes=0x");
              write_hex32(info.inodes);
            } else {
              write_out("\next2: info error");
            }
          } else {
            write_out("\next2: mount failed");
          }
        } else if (str_eq(line, "vfs")) {
          if (!vfs_is_ready()) {
            write_out("\nvfs: no root fs");
          } else {
            write_out("\nvfs: / -> ");
            write_out(vfs_root_name());
          }
        } else if (str_starts(line, "mount ")) {
          const char *p = line + 6;
          while (*p == ' ')
            p++;
          const char *path = p;
          while (*p && *p != ' ')
            p++;
          char mount_path[16];
          size_t i = 0;
          while (path[i] && path[i] != ' ' && i < sizeof(mount_path) - 1) {
            mount_path[i] = path[i];
            i++;
          }
          mount_path[i] = 0;
          while (*p == ' ')
            p++;
          const char *fs = p;
          if (!*mount_path || !*fs) {
            write_out("\nusage: mount <path> <fs>");
          } else {
            if (!vfs_is_ready())
              vfs_init();
            if (vfs_mount(mount_path, fs) != 0)
              write_out("\nmount: failed");
            else
              write_out("\nmount: ok");
          }
        } else if (str_starts(line, "umount ") ||
                   str_starts(line, "unmount ")) {
          const char *path = line + 7;
          if (str_starts(line, "unmount "))
            path = line + 8;
          while (*path == ' ')
            path++;
          if (!*path) {
            write_out("\nusage: umount <path>");
          } else {
            if (vfs_umount(path) != 0)
              write_out("\numount: failed");
            else
              write_out("\numount: ok");
          }
        } else if (str_eq(line, "sync")) {
          write_out(vfs_sync() == 0 ? "\nsync: ok" : "\nsync: failed");
        } else if (str_eq(line, "ls")) {
          if (!vfs_is_ready())
            vfs_init();
          if (!vfs_is_ready()) {
            write_out("\nls: vfs not ready");
          } else {
            write_out("\n/");
            if (vfs_list_dir("/", ls_cb, 0) != 0)
              write_out("\nls: error");
          }
        } else if (str_starts(line, "ls ")) {
          const char *path = line + 3;
          while (*path == ' ')
            path++;
          if (!*path) {
            write_out("\nusage: ls <path>");
          } else {
            if (!vfs_is_ready())
              vfs_init();
            if (!vfs_is_ready()) {
              write_out("\nls: vfs not ready");
            } else {
              if (vfs_list_dir(path, ls_cb, 0) != 0)
                write_out("\nls: error");
            }
          }
        } else if (str_starts(line, "cat ")) {
          const char *name = line + 4;
          while (*name == ' ')
            name++;
          if (!*name) {
            write_out("\nusage: cat <name>");
          } else {
            uint32_t got = 0;
            if (!vfs_is_ready())
              vfs_init();
            if (!vfs_is_ready()) {
              write_out("\ncat: vfs not ready");
            } else {
              const char *path = name;
              char tmp[128];
              if (name[0] != '/') {
                size_t i = 0;
                tmp[i++] = '/';
                while (name[i - 1] && i < sizeof(tmp) - 1) {
                  tmp[i] = name[i - 1];
                  i++;
                }
                tmp[i] = 0;
                path = tmp;
              }
              if (vfs_read_file(path, file_buf, sizeof(file_buf), &got) != 0) {
                write_out("\ncat: not found");
              } else {
                write_out("\n");
                for (uint32_t i = 0; i < got; ++i) {
                  uint8_t c = file_buf[i];
                  if (c == '\n' || c == '\r' || c == '\t' ||
                      (c >= 0x20 && c < 0x7F)) {
                    putc_out((char)c);
                  } else {
                    putc_out('.');
                  }
                }
              }
            }
          }
        } else if (str_starts(line, "open ")) {
          const char *path = line + 5;
          while (*path == ' ')
            path++;
          if (!*path) {
            write_out("\nusage: open <path>");
          } else {
            if (!vfs_is_ready())
              vfs_init();
            if (!vfs_is_ready()) {
              write_out("\nopen: vfs not ready");
            } else {
              int fd = vfs_open(path);
              if (fd < 0) {
                write_out("\nopen: failed");
              } else {
                last_fd = fd;
                write_out("\nfd=");
                write_hex32((uint32_t)fd);
              }
            }
          }
        } else if (str_eq(line, "fread") || str_starts(line, "fread ")) {
          uint32_t fd = (last_fd >= 0) ? (uint32_t)last_fd : 0;
          if (str_starts(line, "fread ")) {
            const char *p = line + 6;
            while (*p == ' ')
              p++;
            if (parse_uint(p, &fd, 0) != 0) {
              write_out("\nusage: fread <fd>");
              goto after_fread;
            }
          } else if (last_fd < 0) {
            write_out("\nfread: no fd");
            goto after_fread;
          }
          {
            uint32_t got = 0;
            if (vfs_read((int)fd, file_buf, sizeof(file_buf), &got) != 0) {
              write_out("\nfread: error");
            } else {
              write_out("\n");
              for (uint32_t i = 0; i < got; ++i) {
                uint8_t c = file_buf[i];
                if (c == '\n' || c == '\r' || c == '\t' ||
                    (c >= 0x20 && c < 0x7F)) {
                  putc_out((char)c);
                } else {
                  putc_out('.');
                }
              }
            }
          }
        after_fread:;
        } else if (str_eq(line, "close") || str_starts(line, "close ")) {
          uint32_t fd = (last_fd >= 0) ? (uint32_t)last_fd : 0;
          if (str_starts(line, "close ")) {
            const char *p = line + 6;
            while (*p == ' ')
              p++;
            if (parse_uint(p, &fd, 0) != 0) {
              write_out("\nusage: close <fd>");
              goto after_close;
            }
          } else if (last_fd < 0) {
            write_out("\nclose: no fd");
            goto after_close;
          }
          if (vfs_close((int)fd) != 0)
            write_out("\nclose: error");
          else {
            write_out("\nclose: ok");
            if ((int)fd == last_fd)
              last_fd = -1;
          }
        after_close:;
        } else if (str_starts(line, "stat ")) {
          const char *path = line + 5;
          while (*path == ' ')
            path++;
          if (!*path) {
            write_out("\nusage: stat <path>");
          } else {
            if (!vfs_is_ready())
              vfs_init();
            if (!vfs_is_ready()) {
              write_out("\nstat: vfs not ready");
            } else {
              vfs_stat_t st;
              if (vfs_stat(path, &st) != 0) {
                write_out("\nstat: not found");
              } else {
                write_out("\nstat: ");
                if (st.type == 1)
                  write_out("file ");
                else if (st.type == 2)
                  write_out("dir ");
                else
                  write_out("other ");
                write_out("size=0x");
                write_hex32(st.size);
              }
            }
          }
        } else if (str_starts(line, "read ")) {
          const char *p = line + 5;
          uint32_t lba = 0, count = 1;
          if (parse_uint(p, &lba, &p) != 0) {
            write_out("\nusage: read <lba> [count]");
          } else {
            if (parse_uint(p, &count, 0) != 0)
              count = 1;
            if (count == 0 || count > 4) {
              write_out("\nread: count 1-4");
            } else if (ata_read28(lba, (uint8_t)count, read_buf) != 0) {
              write_out("\nread: error");
            } else {
              write_out("\nread ok, first 64 bytes:");
              uint32_t total = count * 512;
              uint32_t limit = total < 64 ? total : 64;
              for (uint32_t i = 0; i < limit; ++i) {
                if ((i % 16) == 0)
                  write_out("\n");
                write_hex8(read_buf[i]);
                putc_out(' ');
              }
            }
          }
        } else if (str_eq(line, "mouse")) {
          struct mouse_packet p;
          if (!mouse_read_packet(&p)) {
            p = mouse_peek_last();
          }
          write_out("\nMouse dx=");
          int vals[2] = {p.dx, p.dy};
          for (int a = 0; a < 2; ++a) {
            int v = vals[a];
            if (v < 0) {
              putc_out('-');
              v = -v;
            } else {
              putc_out('+');
            }
            char buf[4];
            int pos = 0;
            if (v == 0) {
              buf[pos++] = '0';
            } else {
              while (v > 0 && pos < 3) {
                buf[pos++] = '0' + (v % 10);
                v /= 10;
              }
            }
            for (int i = pos - 1; i >= 0; --i)
              putc_out(buf[i]);
            if (a == 0)
              write_out(" dy=");
          }
          write_out(" btn=");
          for (int b = 0; b < 3; ++b)
            putc_out((p.buttons & (1 << b)) ? '1' : '0');
        } else {
          write_out("\nunknown command");
        }
        history_push(line);
      }
      len = 0;
      cursor = 0;
      prev_len = 0;
      hist_pos = -1;
      print_prompt();
      continue;
    } else if (ch >= 0x20 && ch < 0x7F) {
      if (len + 1 < sizeof(line)) {
        for (size_t i = len; i > cursor; --i) {
          line[i] = line[i - 1];
        }
        line[cursor] = (char)ch;
        len++;
        cursor++;
      }
    } else {
      // ignore other keys for now
    }

    // redraw current input line
    write_out("\r");
    write_out(prompt);
    for (size_t i = 0; i < len; ++i)
      putc_out(line[i]);
    if (prev_len > len) {
      size_t diff = prev_len - len;
      while (diff--)
        putc_out(' ');
    }
    // position cursor: restart line and advance to cursor
    write_out("\r");
    write_out(prompt);
    for (size_t i = 0; i < cursor; ++i)
      putc_out(line[i]);
    tty_draw_cursor();
    if (framebuffer.addr == NULL) {
      int mx, my;
      if (mouse_get_pos(&mx, &my)) {
        uint32_t col = (uint32_t)(mx / 8);
        uint32_t row = (uint32_t)(my / 16);
        console_mouse_update(col, row);
      }
    }
    prev_len = len;
  }
}
