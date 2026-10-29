#include "mini_libc.h"
#include "syscall.h"

enum {
  TTY_COLOR_BLACK = 0,
  TTY_COLOR_BLUE = 9,
  TTY_COLOR_GREEN = 10,
  TTY_COLOR_CYAN = 11,
  TTY_COLOR_RED = 12,
  TTY_COLOR_WHITE = 15,
};

static const char *prompt = "hydrogen$ ";

static void write_prompt(void);

#define HISTORY_CAP 16
#define LINE_CAP 128

static char history[HISTORY_CAP][LINE_CAP];
static int history_count = 0;
static int history_next = 0;

static void set_shell_color(unsigned char fg) {
  tty_set_color(fg, TTY_COLOR_BLACK);
}

static void write_str(const char *s) {
  if (!s)
    return;
  write(1, s, strlen(s));
}

static void write_ch(char c) { write(1, &c, 1); }

static void write_u64(unsigned long v) {
  char buf[24];
  int i = 0;
  if (v == 0) {
    write_ch('0');
    return;
  }
  while (v > 0 && i < (int)sizeof(buf)) {
    buf[i++] = (char)('0' + (v % 10));
    v /= 10;
  }
  while (i > 0) {
    write_ch(buf[--i]);
  }
}

static void write_poll_mask(long mask) {
  int wrote = 0;
  if ((mask & POLL_READ) != 0) {
    write_str("READ");
    wrote = 1;
  }
  if ((mask & POLL_WRITE) != 0) {
    if (wrote)
      write_ch('|');
    write_str("WRITE");
    wrote = 1;
  }
  if ((mask & POLL_HUP) != 0) {
    if (wrote)
      write_ch('|');
    write_str("HUP");
    wrote = 1;
  }
  if ((mask & POLL_NVAL) != 0) {
    if (wrote)
      write_ch('|');
    write_str("NVAL");
    wrote = 1;
  }
  if (!wrote) {
    write_str("none");
  }
}

static void write_error(const char *s) {
  set_shell_color(TTY_COLOR_RED);
  write_str(s);
  set_shell_color(TTY_COLOR_WHITE);
}

static void write_info(const char *s) {
  set_shell_color(TTY_COLOR_CYAN);
  write_str(s);
  set_shell_color(TTY_COLOR_WHITE);
}

static void write_success(const char *s) {
  set_shell_color(TTY_COLOR_GREEN);
  write_str(s);
  set_shell_color(TTY_COLOR_WHITE);
}

static int str_eq(const char *a, const char *b) {
  if (!a || !b)
    return 0;
  while (*a && *b) {
    if (*a != *b)
      return 0;
    ++a;
    ++b;
  }
  return *a == 0 && *b == 0;
}

static char *skip_ws(char *s) {
  if (!s)
    return s;
  while (*s == ' ' || *s == '\t')
    s++;
  return s;
}

static unsigned long parse_u64(const char *s) {
  unsigned long v = 0;
  if (!s)
    return 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (unsigned long)(*s - '0');
    ++s;
  }
  return v;
}

static int read_char_blocking(char *out) {
  while (1) {
    long n = read(0, out, 1);
    if (n > 0) {
      return 1;
    }
    yield_cpu();
  }
}

static void redraw_line(const char *buf, int len, int cursor) {
  write_ch('\r');
  write_prompt();
  set_shell_color(TTY_COLOR_WHITE);
  if (len > 0) {
    write(1, buf, (size_t)len);
  }
  write_ch(' ');
  write_ch('\r');
  write_prompt();
  set_shell_color(TTY_COLOR_WHITE);
  if (cursor > 0) {
    write(1, buf, (size_t)cursor);
  }
}

static void copy_line(char *dst, const char *src, int cap) {
  int i = 0;
  if (!dst || cap <= 0)
    return;
  if (!src) {
    dst[0] = 0;
    return;
  }
  while (src[i] && i + 1 < cap) {
    dst[i] = src[i];
    ++i;
  }
  dst[i] = 0;
}

static int line_len(const char *s) {
  int n = 0;
  if (!s)
    return 0;
  while (s[n])
    ++n;
  return n;
}

static void history_push(const char *line) {
  int last_index;
  if (!line || !*line)
    return;
  if (history_count > 0) {
    last_index = (history_next + HISTORY_CAP - 1) % HISTORY_CAP;
    if (str_eq(history[last_index], line))
      return;
  }
  copy_line(history[history_next], line, LINE_CAP);
  history_next = (history_next + 1) % HISTORY_CAP;
  if (history_count < HISTORY_CAP)
    history_count++;
}

static const char *history_get(int browse_index) {
  int oldest;
  int slot;
  if (browse_index < 0 || browse_index >= history_count)
    return 0;
  oldest = (history_next - history_count + HISTORY_CAP) % HISTORY_CAP;
  slot = (oldest + browse_index) % HISTORY_CAP;
  return history[slot];
}

static int read_line(char *buf, int cap) {
  int len = 0;
  int cursor = 0;
  int history_index = history_count;
  char draft[LINE_CAP];
  draft[0] = 0;
  for (;;) {
    char c = 0;
    if (!read_char_blocking(&c))
      continue;
    if (c == '\r')
      c = '\n';
    if (c == '\n') {
      write_ch('\n');
      if (len < cap)
        buf[len] = 0;
      history_push(buf);
      return len;
    }
    if (c == 27) {
      char seq1 = 0;
      char seq2 = 0;
      if (!read_char_blocking(&seq1))
        continue;
      if (seq1 != '[')
        continue;
      if (!read_char_blocking(&seq2))
        continue;
      if (seq2 == 'D') {
        if (cursor > 0) {
          cursor--;
          redraw_line(buf, len, cursor);
        }
        continue;
      }
      if (seq2 == 'C') {
        if (cursor < len) {
          cursor++;
          redraw_line(buf, len, cursor);
        }
        continue;
      }
      if (seq2 == 'A') {
        const char *entry;
        if (history_count == 0 || history_index <= 0)
          continue;
        if (history_index == history_count) {
          copy_line(draft, buf, LINE_CAP);
        }
        history_index--;
        entry = history_get(history_index);
        if (!entry)
          continue;
        copy_line(buf, entry, cap);
        len = line_len(buf);
        cursor = len;
        redraw_line(buf, len, cursor);
        continue;
      }
      if (seq2 == 'B') {
        const char *entry;
        if (history_count == 0 || history_index >= history_count)
          continue;
        history_index++;
        if (history_index == history_count) {
          copy_line(buf, draft, cap);
        } else {
          entry = history_get(history_index);
          if (!entry)
            continue;
          copy_line(buf, entry, cap);
        }
        len = line_len(buf);
        cursor = len;
        redraw_line(buf, len, cursor);
        continue;
      }
      if (seq2 == 'H') {
        cursor = 0;
        redraw_line(buf, len, cursor);
        continue;
      }
      if (seq2 == 'F') {
        cursor = len;
        redraw_line(buf, len, cursor);
        continue;
      }
      if (seq2 == '3') {
        char seq3 = 0;
        if (!read_char_blocking(&seq3))
          continue;
        if (seq3 == '~' && cursor < len) {
          for (int i = cursor; i + 1 < len; ++i) {
            buf[i] = buf[i + 1];
          }
          len--;
          redraw_line(buf, len, cursor);
        }
        continue;
      }
      continue;
    }
    if (c == '\b' || c == 127) {
      if (cursor > 0) {
        for (int i = cursor - 1; i + 1 < len; ++i) {
          buf[i] = buf[i + 1];
        }
        len--;
        cursor--;
        redraw_line(buf, len, cursor);
      }
      continue;
    }
    if (c >= 0x20) {
      if (len + 1 < cap) {
        for (int i = len; i > cursor; --i) {
          buf[i] = buf[i - 1];
        }
        buf[cursor] = c;
        len++;
        cursor++;
        history_index = history_count;
        redraw_line(buf, len, cursor);
      }
    }
  }
}

static void cmd_help(void) {
  write_info("Commands: ");
  write_str("help, time, sleep [ms], cat <path>, ls [path], mkdir <path>, rm <path>, "
            "touch <path>, mv <src> <dst>, cp <src> <dst>, echo <text>, pipe [text], "
            "piperun [text], pipenoinherit [text], pipeblock [text], pipefull, "
            "pipebroken, pipeeof, pollpipe, pollwaitpipe, pollmanypipe, run "
            "<path>, cd <path>, pwd, exit\n");
}

static void cmd_pwd(void) {
  char buf[256];
  char *cwd = getcwd(buf, sizeof(buf));
  if (!cwd) {
    write_error("pwd: failed\n");
    return;
  }
  write_str(cwd);
  write_ch('\n');
}

static void cmd_cd(const char *path) {
  if (!path || !*path) {
    write_error("usage: cd <path>\n");
    return;
  }
  if (chdir(path) != 0) {
    write_error("cd: no such directory: ");
    write_str(path);
    write_ch('\n');
  }
}

static void write_prompt(void) {
  set_shell_color(TTY_COLOR_BLUE);
  write_str(prompt);
  set_shell_color(TTY_COLOR_WHITE);
}

static void cmd_time(void) {
  write_str("time_ms: ");
  write_u64(time_ms());
  write_ch('\n');
}

static void cmd_sleep(const char *arg) {
  unsigned long ms = 1000;
  if (arg && *arg)
    ms = parse_u64(arg);
  sleep_ms(ms);
  write_str("done\n");
}

static void cmd_echo(const char *arg) {
  if (arg && *arg) {
    write_str(arg);
  }
  write_ch('\n');
}

static void cmd_pipe(const char *arg) {
  int fds[2] = {-1, -1};
  const char *msg = (arg && *arg) ? arg : "hello through pipe";
  char buf[128];
  if (pipe(fds) != 0) {
    write_error("pipe: create failed\n");
    return;
  }
  long wrote = write(fds[1], msg, strlen(msg));
  if (wrote < 0) {
    write_error("pipe: write failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  long got = read(fds[0], buf, sizeof(buf) - 1);
  if (got < 0) {
    write_error("pipe: read failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  buf[got] = 0;
  write_info("pipe read: ");
  write_str(buf);
  write_ch('\n');
  close(fds[0]);
  close(fds[1]);
}

static void cmd_piperun(const char *arg) {
  int fds[2] = {-1, -1};
  const char *msg = (arg && *arg) ? arg : "hello child over inherited pipe";
  if (pipe(fds) != 0) {
    write_error("piperun: create failed\n");
    return;
  }
  if (set_handle_inherit(fds[0], 1) != 0) {
    write_error("piperun: mark inherit failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  long wrote = write(fds[1], msg, strlen(msg));
  if (wrote < 0) {
    write_error("piperun: write failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  long pid = spawn("/piperead");
  if (pid < 0) {
    write_error("piperun: spawn failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  int status = 0;
  long waited = wait_pid((int)pid, &status);
  close(fds[0]);
  close(fds[1]);
  if (waited < 0) {
    write_error("piperun: wait failed\n");
    return;
  }
  write_info("piperun: child ");
  write_u64((unsigned long)pid);
  write_str(" status ");
  write_u64((unsigned long)status);
  write_ch('\n');
}

static void cmd_pipenoinherit(const char *arg) {
  int fds[2] = {-1, -1};
  const char *msg = (arg && *arg) ? arg : "hidden from child";
  if (pipe(fds) != 0) {
    write_error("pipenoinherit: create failed\n");
    return;
  }
  if (write(fds[1], msg, strlen(msg)) < 0) {
    write_error("pipenoinherit: write failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  long pid = spawn("/piperead");
  if (pid < 0) {
    write_error("pipenoinherit: spawn failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  int status = 0;
  long waited = wait_pid((int)pid, &status);
  close(fds[0]);
  close(fds[1]);
  if (waited < 0) {
    write_error("pipenoinherit: wait failed\n");
    return;
  }
  write_info("pipenoinherit: child ");
  write_u64((unsigned long)pid);
  write_str(" status ");
  write_u64((unsigned long)status);
  write_ch('\n');
}

static void cmd_pipeeof(void) {
  int fds[2] = {-1, -1};
  if (pipe(fds) != 0) {
    write_error("pipeeof: create failed\n");
    return;
  }
  if (set_handle_inherit(fds[0], 1) != 0) {
    write_error("pipeeof: mark inherit failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  close(fds[1]);
  long pid = spawn("/piperead");
  if (pid < 0) {
    write_error("pipeeof: spawn failed\n");
    close(fds[0]);
    return;
  }
  int status = 0;
  long waited = wait_pid((int)pid, &status);
  close(fds[0]);
  if (waited < 0) {
    write_error("pipeeof: wait failed\n");
    return;
  }
  write_info("pipeeof: child ");
  write_u64((unsigned long)pid);
  write_str(" status ");
  write_u64((unsigned long)status);
  write_ch('\n');
}

static void cmd_pipeblock(const char *arg) {
  int fds[2] = {-1, -1};
  const char *msg = (arg && *arg) ? arg : "delayed data";
  if (pipe(fds) != 0) {
    write_error("pipeblock: create failed\n");
    return;
  }
  if (set_handle_inherit(fds[0], 1) != 0) {
    write_error("pipeblock: mark inherit failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  long pid = spawn("/piperead");
  if (pid < 0) {
    write_error("pipeblock: spawn failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  sleep_ms(50);
  if (write(fds[1], msg, strlen(msg)) < 0) {
    write_error("pipeblock: write failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  int status = 0;
  long waited = wait_pid((int)pid, &status);
  close(fds[0]);
  close(fds[1]);
  if (waited < 0) {
    write_error("pipeblock: wait failed\n");
    return;
  }
  write_info("pipeblock: child ");
  write_u64((unsigned long)pid);
  write_str(" status ");
  write_u64((unsigned long)status);
  write_ch('\n');
}

static void cmd_pipefull(void) {
  int fds[2] = {-1, -1};
  char fill[512];
  long start = 0;
  long delta = 0;
  int status = 0;
  for (int i = 0; i < (int)sizeof(fill); ++i) {
    fill[i] = 'A';
  }
  if (pipe(fds) != 0) {
    write_error("pipefull: create failed\n");
    return;
  }
  if (set_handle_inherit(fds[0], 1) != 0) {
    write_error("pipefull: mark inherit failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  if (write(fds[1], fill, sizeof(fill)) != (long)sizeof(fill)) {
    write_error("pipefull: initial fill failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  long pid = spawn("/pipedelayread");
  if (pid < 0) {
    write_error("pipefull: spawn failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  start = (long)time_ms();
  if (write(fds[1], "TAIL", 4) != 4) {
    write_error("pipefull: tail write failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }
  delta = (long)time_ms() - start;
  close(fds[1]);
  if (wait_pid((int)pid, &status) < 0) {
    write_error("pipefull: wait failed\n");
    close(fds[0]);
    return;
  }
  close(fds[0]);
  write_info("pipefull: child ");
  write_u64((unsigned long)pid);
  write_str(" status ");
  write_u64((unsigned long)status);
  write_str(", blocked ");
  write_u64((unsigned long)delta);
  write_str(" ms\n");
}

static void cmd_pipebroken(void) {
  int fds[2] = {-1, -1};
  long rc = 0;
  if (pipe(fds) != 0) {
    write_error("pipebroken: create failed\n");
    return;
  }
  close(fds[0]);
  rc = write(fds[1], "X", 1);
  close(fds[1]);
  if (rc >= 0) {
    write_error("pipebroken: expected write failure\n");
    return;
  }
  write_info("pipebroken: write rc ");
  write_u64((unsigned long)(-rc));
  write_str(" (negative errno)\n");
}

static void cmd_pollpipe(void) {
  int fds[2] = {-1, -1};
  char byte = 0;
  long ready = 0;

  if (pipe(fds) != 0) {
    write_error("pollpipe: create failed\n");
    return;
  }

  write_info("pollpipe: reader initial ");
  ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
  write_poll_mask(ready);
  write_ch('\n');

  write_info("pollpipe: writer initial ");
  ready = poll_fd(fds[1], POLL_WRITE | POLL_HUP);
  write_poll_mask(ready);
  write_ch('\n');

  if (write(fds[1], "x", 1) != 1) {
    write_error("pollpipe: write failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }

  write_info("pollpipe: reader after write ");
  ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
  write_poll_mask(ready);
  write_ch('\n');

  close(fds[1]);
  write_info("pollpipe: reader after writer close ");
  ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
  write_poll_mask(ready);
  write_ch('\n');

  if (read(fds[0], &byte, 1) != 1) {
    write_error("pollpipe: read failed\n");
    close(fds[0]);
    return;
  }
  write_info("pollpipe: read byte ");
  write_ch(byte);
  write_ch('\n');

  write_info("pollpipe: reader after drain ");
  ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
  write_poll_mask(ready);
  write_ch('\n');
  close(fds[0]);

  if (pipe(fds) != 0) {
    write_error("pollpipe: second create failed\n");
    return;
  }
  close(fds[0]);
  write_info("pollpipe: writer after reader close ");
  ready = poll_fd(fds[1], POLL_WRITE | POLL_HUP);
  write_poll_mask(ready);
  write_ch('\n');
  close(fds[1]);
}

static void cmd_pollwaitpipe(void) {
  int fds[2] = {-1, -1};
  long pid = 0;
  long ready = 0;
  unsigned long start = 0;
  unsigned long elapsed = 0;
  int status = 0;
  char byte = 0;

  if (pipe(fds) != 0) {
    write_error("pollwaitpipe: create failed\n");
    return;
  }

  start = time_ms();
  ready = poll_wait_fd(fds[0], POLL_READ | POLL_HUP, 40);
  elapsed = time_ms() - start;
  write_info("pollwaitpipe: timeout wait ");
  write_poll_mask(ready);
  write_str(" after ");
  write_u64(elapsed);
  write_str(" ms\n");

  pid = spawn_with_stdio("/pollwakewriter", SPAWN_STDIO_NULL, fds[1],
                         SPAWN_STDIO_NULL);
  if (pid < 0) {
    write_error("pollwaitpipe: spawn failed\n");
    close(fds[0]);
    close(fds[1]);
    return;
  }

  start = time_ms();
  ready = poll_wait_fd(fds[0], POLL_READ | POLL_HUP, 500);
  elapsed = time_ms() - start;
  write_info("pollwaitpipe: wake wait ");
  write_poll_mask(ready);
  write_str(" after ");
  write_u64(elapsed);
  write_str(" ms\n");

  if ((ready & POLL_READ) != 0 && read(fds[0], &byte, 1) == 1) {
    write_info("pollwaitpipe: read byte ");
    write_ch(byte);
    write_ch('\n');
  }

  close(fds[1]);
  if (wait_pid((int)pid, &status) < 0) {
    write_error("pollwaitpipe: wait failed\n");
    close(fds[0]);
    return;
  }

  start = time_ms();
  ready = poll_wait_fd(fds[0], POLL_READ | POLL_HUP, 100);
  elapsed = time_ms() - start;
  write_info("pollwaitpipe: eof wait ");
  write_poll_mask(ready);
  write_str(" after ");
  write_u64(elapsed);
  write_str(" ms\n");

  close(fds[0]);
}

static void cmd_pollmanypipe(void) {
  int left[2] = {-1, -1};
  int right[2] = {-1, -1};
  poll_fd_t fds[2];
  long pid = 0;
  long ready = 0;
  int status = 0;
  char byte = 0;

  if (pipe(left) != 0 || pipe(right) != 0) {
    write_error("pollmanypipe: create failed\n");
    if (left[0] >= 0) close(left[0]);
    if (left[1] >= 0) close(left[1]);
    if (right[0] >= 0) close(right[0]);
    if (right[1] >= 0) close(right[1]);
    return;
  }

  fds[0].fd = left[0];
  fds[0].events = POLL_READ | POLL_HUP;
  fds[0].revents = 0;
  fds[1].fd = right[0];
  fds[1].events = POLL_READ | POLL_HUP;
  fds[1].revents = 0;

  ready = poll_many_wait(fds, 2, 0);
  write_info("pollmanypipe: snapshot ready ");
  write_u64((unsigned long)ready);
  write_str(", left ");
  write_poll_mask(fds[0].revents);
  write_str(", right ");
  write_poll_mask(fds[1].revents);
  write_ch('\n');

  fds[0].revents = 0;
  fds[1].revents = 0;
  ready = poll_many_wait(fds, 2, 40);
  write_info("pollmanypipe: timeout ready ");
  write_u64((unsigned long)ready);
  write_str(", left ");
  write_poll_mask(fds[0].revents);
  write_str(", right ");
  write_poll_mask(fds[1].revents);
  write_ch('\n');

  pid = spawn_with_stdio("/pollwakewriter", SPAWN_STDIO_NULL, right[1],
                         SPAWN_STDIO_NULL);
  if (pid < 0) {
    write_error("pollmanypipe: spawn failed\n");
    close(left[0]);
    close(left[1]);
    close(right[0]);
    close(right[1]);
    return;
  }

  close(right[1]);
  fds[0].revents = 0;
  fds[1].revents = 0;
  ready = poll_many_wait(fds, 2, 500);
  write_info("pollmanypipe: wake ready ");
  write_u64((unsigned long)ready);
  write_str(", left ");
  write_poll_mask(fds[0].revents);
  write_str(", right ");
  write_poll_mask(fds[1].revents);
  write_ch('\n');

  if ((fds[1].revents & POLL_READ) != 0 && read(right[0], &byte, 1) == 1) {
    write_info("pollmanypipe: read byte ");
    write_ch(byte);
    write_ch('\n');
  }

  if (wait_pid((int)pid, &status) < 0) {
    write_error("pollmanypipe: wait failed\n");
    close(left[0]);
    close(left[1]);
    close(right[0]);
    return;
  }

  close(left[1]);
  fds[0].revents = 0;
  fds[1].revents = 0;
  ready = poll_many_wait(fds, 2, 100);
  write_info("pollmanypipe: eof ready ");
  write_u64((unsigned long)ready);
  write_str(", left ");
  write_poll_mask(fds[0].revents);
  write_str(", right ");
  write_poll_mask(fds[1].revents);
  write_ch('\n');

  close(left[0]);
  close(right[0]);
}

static void cmd_cat(const char *path) {
  if (!path || !*path) {
    write_error("usage: cat <path>\n");
    return;
  }
  int fd = (int)open(path, 0);
  if (fd < 0) {
    write_error("cat: open failed\n");
    return;
  }
  char buf[256];
  long n = read(fd, buf, sizeof(buf));
  if (n > 0) {
    write(1, buf, (size_t)n);
    if (n == (long)sizeof(buf)) {
      write_str("\n[truncated]\n");
    }
  } else {
    write_error("cat: read failed\n");
  }
  close(fd);
}

static void cmd_ls(const char *path) {
  const char *target = (path && *path) ? path : ".";
  int pos = 0;
  dirent_t de;
  while (readdir_pos(target, pos, &de) == 1) {
    if (de.d_type == 4) {
      write_info(de.d_name);
      write_str("/\n");
    } else {
      write_str(de.d_name);
      write_str("\n");
    }
    ++pos;
  }
}

static void cmd_mkdir(const char *path) {
  if (!path || !*path) {
    write_error("usage: mkdir <path>\n");
    return;
  }
  if (sys_mkdir(path) != 0) {
    write_error("mkdir: failed\n");
    return;
  }
  write_success("mkdir: ok\n");
}

static void cmd_rm(const char *path) {
  if (!path || !*path) {
    write_error("usage: rm <path>\n");
    return;
  }
  if (sys_unlink(path) != 0) {
    write_error("rm: failed\n");
    return;
  }
  write_success("rm: ok\n");
}

static void cmd_touch(const char *path) {
  if (!path || !*path) {
    write_error("usage: touch <path>\n");
    return;
  }
  /* create empty file if not exists; overwrite allowed */
  if (sys_writef(path, "", 0) != 0) {
    write_error("touch: failed\n");
    return;
  }
  write_success("touch: ok\n");
}

static void cmd_mv(const char *arg) {
  char src[128];
  char dst[128];
  /* split arg into two tokens */
  const char *p = arg;
  if (!arg || !*arg) {
    write_error("usage: mv <src> <dst>\n");
    return;
  }
  int i = 0;
  while (*p && *p != ' ' && *p != '\t' && i < 127) src[i++] = *p++;
  src[i] = 0;
  p = skip_ws((char *)p);
  i = 0;
  while (*p && *p != ' ' && *p != '\t' && i < 127) dst[i++] = *p++;
  dst[i] = 0;
  if (!src[0] || !dst[0]) {
    write_error("usage: mv <src> <dst>\n");
    return;
  }
  if (sys_rename(src, dst) != 0) {
    write_error("mv: failed\n");
    return;
  }
  write_success("mv: ok\n");
}

static void cmd_cp(const char *arg) {
  char src[128];
  char dst[128];
  const char *p = arg;
  if (!arg || !*arg) {
    write_error("usage: cp <src> <dst>\n");
    return;
  }
  int i = 0;
  while (*p && *p != ' ' && *p != '\t' && i < 127) src[i++] = *p++;
  src[i] = 0;
  p = skip_ws((char *)p);
  i = 0;
  while (*p && *p != ' ' && *p != '\t' && i < 127) dst[i++] = *p++;
  dst[i] = 0;
  if (!src[0] || !dst[0]) {
    write_error("usage: cp <src> <dst>\n");
    return;
  }
  int fd = (int)open(src, 0);
  if (fd < 0) {
    write_error("cp: open src failed\n");
    return;
  }
  char buf[256];
  long n;
  char data[4096];
  int data_len = 0;
  while ((n = read(fd, buf, sizeof(buf))) > 0) {
    for (long k = 0; k < n && data_len < (int)sizeof(data) - 1; ++k) {
      data[data_len++] = buf[k];
    }
  }
  close(fd);
  if (sys_writef(dst, data, (unsigned long)data_len) != 0) {
    write_error("cp: write dst failed\n");
    return;
  }
  write_success("cp: ok\n");
}

static void cmd_run(const char *path) {
  if (!path || !*path) {
    write_error("usage: run <path>\n");
    return;
  }
  long pid = spawn(path);
  if (pid < 0) {
    write_error("run: spawn failed\n");
    return;
  }
  int status = 0;
  long waited = wait_pid((int)pid, &status);
  if (waited < 0) {
    write_error("run: wait failed\n");
    return;
  }
  write_info("run: pid ");
  write_u64((unsigned long)pid);
  if (status == 0) {
    write_success(" exited with status ");
  } else {
    write_error(" exited with status ");
  }
  write_u64((unsigned long)status);
  write_ch('\n');
}

int main(void) {
  char line[128];
  set_shell_color(TTY_COLOR_CYAN);
  write_str("HydrogenOS");
  set_shell_color(TTY_COLOR_WHITE);
  write_str(" user shell. Type ");
  set_shell_color(TTY_COLOR_CYAN);
  write_str("'help'");
  set_shell_color(TTY_COLOR_WHITE);
  write_str(".\n");
  for (;;) {
    write_prompt();
    int len = read_line(line, (int)sizeof(line));
    if (len <= 0)
      continue;
    
    /* Simple Pipe Handling: cmd1 | cmd2 */
    char *pipe_ptr = (char *)0;
    for (int i = 0; i < len; i++) {
        if (line[i] == '|') {
            line[i] = 0;
            pipe_ptr = &line[i+1];
            break;
        }
    }

    /* Redirection Handling: cmd > file or cmd >> file */
    char *redir_out = (char *)0;
    int append = 0;
    for (int i = 0; i < len; i++) {
        if (line[i] == '>') {
            line[i] = 0;
            if (line[i+1] == '>') {
                line[i+1] = 0;
                append = 1;
                redir_out = skip_ws(&line[i+2]);
            } else {
                redir_out = skip_ws(&line[i+1]);
            }
            break;
        }
    }

    if (pipe_ptr) {
        int fds[2];
        if (pipe(fds) != 0) {
            write_error("shell: pipe failed\n");
            continue;
        }
        char *cmd1 = skip_ws(line);
        char *cmd2 = skip_ws(pipe_ptr);
        
        long pid1 = spawn_with_stdio(cmd1, -2, fds[1], -2);
        long pid2 = spawn_with_stdio(cmd2, fds[0], -2, -2);
        
        close(fds[0]);
        close(fds[1]);
        
        if (pid1 > 0) wait_pid((int)pid1, (int *)0);
        if (pid2 > 0) wait_pid((int)pid2, (int *)0);
        continue;
    }

    char *cmd = skip_ws(line);
    if (!*cmd)
      continue;
    
    int old_stdout = -1;
    if (redir_out && *redir_out) {
        /* Redirection: we need to temporarily replace stdout */
        int fd = (int)open(redir_out, 0); // flags ignored for now in simple ramfs
        if (fd < 0) {
            write_error("shell: redirection failed\n");
            continue;
        }
        old_stdout = (int)dup(1);
        dup2(fd, 1);
        close(fd);
    }

    char *arg = cmd;
    while (*arg && *arg != ' ' && *arg != '\t')
      arg++;
    if (*arg) {
      *arg = 0;
      arg = skip_ws(arg + 1);
    } else {
      arg = (char *)"";
    }

    if (str_eq(cmd, "help")) {
      cmd_help();
    } else if (str_eq(cmd, "time")) {
      cmd_time();
    } else if (str_eq(cmd, "sleep")) {
      cmd_sleep(arg);
    } else if (str_eq(cmd, "cat")) {
      cmd_cat(arg);
    } else if (str_eq(cmd, "ls")) {
      cmd_ls(arg);
    } else if (str_eq(cmd, "mkdir")) {
      cmd_mkdir(arg);
    } else if (str_eq(cmd, "rm")) {
      cmd_rm(arg);
    } else if (str_eq(cmd, "touch")) {
      cmd_touch(arg);
    } else if (str_eq(cmd, "mv")) {
      cmd_mv(arg);
    } else if (str_eq(cmd, "cp")) {
      cmd_cp(arg);
    } else if (str_eq(cmd, "echo")) {
      cmd_echo(arg);
    } else if (str_eq(cmd, "pipe")) {
      cmd_pipe(arg);
    } else if (str_eq(cmd, "piperun")) {
      cmd_piperun(arg);
    } else if (str_eq(cmd, "pipenoinherit")) {
      cmd_pipenoinherit(arg);
    } else if (str_eq(cmd, "pipeblock")) {
      cmd_pipeblock(arg);
    } else if (str_eq(cmd, "pipefull")) {
      cmd_pipefull();
    } else if (str_eq(cmd, "pipebroken")) {
      cmd_pipebroken();
    } else if (str_eq(cmd, "pipeeof")) {
      cmd_pipeeof();
    } else if (str_eq(cmd, "pollpipe")) {
      cmd_pollpipe();
    } else if (str_eq(cmd, "pollwaitpipe")) {
      cmd_pollwaitpipe();
    } else if (str_eq(cmd, "pollmanypipe")) {
      cmd_pollmanypipe();
    } else if (str_eq(cmd, "run") || str_eq(cmd, "spawn")) {
      cmd_run(arg);
    } else if (str_eq(cmd, "cd")) {
      cmd_cd(arg);
    } else if (str_eq(cmd, "pwd")) {
      cmd_pwd();
    } else if (str_eq(cmd, "exit")) {
      return 0;
    } else {
      write_error("Unknown command. ");
      write_str("Type ");
      write_str("'help'");
      write_str(".\n");
    }

    if (old_stdout != -1) {
        dup2(old_stdout, 1);
        close(old_stdout);
    }
  }
  return 0;
}
