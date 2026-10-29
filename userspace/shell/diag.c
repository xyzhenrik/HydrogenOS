#include "diag.h"

#include "mini_libc.h"
#include "output.h"

/* Interactive probes for the pipe, inheritance and poll primitives. They
 * print what the kernel reports and spawn the helper programs /piperead,
 * /pipedelayread and /pollwakewriter. */

static void write_poll_mask(long mask) {
  int wrote = 0;
  if ((mask & POLL_READ) != 0) {
    out_str(1, "READ");
    wrote = 1;
  }
  if ((mask & POLL_WRITE) != 0) {
    if (wrote)
      out_char(1, '|');
    out_str(1, "WRITE");
    wrote = 1;
  }
  if ((mask & POLL_HUP) != 0) {
    if (wrote)
      out_char(1, '|');
    out_str(1, "HUP");
    wrote = 1;
  }
  if ((mask & POLL_NVAL) != 0) {
    if (wrote)
      out_char(1, '|');
    out_str(1, "NVAL");
    wrote = 1;
  }
  if (!wrote) {
    out_str(1, "none");
  }
}

static int diag_pipe(int argc, char **argv) {
  const char *arg = argc > 1 ? argv[1] : 0;
  int fds[2] = {-1, -1};
  const char *msg = (arg && *arg) ? arg : "hello through pipe";
  char buf[128];
  if (pipe(fds) != 0) {
    out_str(2, "pipe: create failed\n");
    return 1;
  }
  long wrote = write(fds[1], msg, strlen(msg));
  if (wrote < 0) {
    out_str(2, "pipe: write failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  long got = read(fds[0], buf, sizeof(buf) - 1);
  if (got < 0) {
    out_str(2, "pipe: read failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  buf[got] = 0;
  out_str(1, "pipe read: ");
  out_str(1, buf);
  out_char(1, '\n');
  close(fds[0]);
  close(fds[1]);
  return 0;
}

static int diag_piperun(int argc, char **argv) {
  const char *arg = argc > 1 ? argv[1] : 0;
  int fds[2] = {-1, -1};
  const char *msg = (arg && *arg) ? arg : "hello child over inherited pipe";
  if (pipe(fds) != 0) {
    out_str(2, "piperun: create failed\n");
    return 1;
  }
  if (set_handle_inherit(fds[0], 1) != 0) {
    out_str(2, "piperun: mark inherit failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  long wrote = write(fds[1], msg, strlen(msg));
  if (wrote < 0) {
    out_str(2, "piperun: write failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  out_flush();
  long pid = spawn("/piperead");
  if (pid < 0) {
    out_str(2, "piperun: spawn failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  int status = 0;
  long waited = wait_pid((int)pid, &status);
  close(fds[0]);
  close(fds[1]);
  if (waited < 0) {
    out_str(2, "piperun: wait failed\n");
    return 1;
  }
  out_str(1, "piperun: child ");
  out_u64(1, (unsigned long)pid);
  out_str(1, " status ");
  out_u64(1, (unsigned long)status);
  out_char(1, '\n');
  return 0;
}

static int diag_pipenoinherit(int argc, char **argv) {
  const char *arg = argc > 1 ? argv[1] : 0;
  int fds[2] = {-1, -1};
  const char *msg = (arg && *arg) ? arg : "hidden from child";
  if (pipe(fds) != 0) {
    out_str(2, "pipenoinherit: create failed\n");
    return 1;
  }
  if (write(fds[1], msg, strlen(msg)) < 0) {
    out_str(2, "pipenoinherit: write failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  out_flush();
  long pid = spawn("/piperead");
  if (pid < 0) {
    out_str(2, "pipenoinherit: spawn failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  int status = 0;
  long waited = wait_pid((int)pid, &status);
  close(fds[0]);
  close(fds[1]);
  if (waited < 0) {
    out_str(2, "pipenoinherit: wait failed\n");
    return 1;
  }
  out_str(1, "pipenoinherit: child ");
  out_u64(1, (unsigned long)pid);
  out_str(1, " status ");
  out_u64(1, (unsigned long)status);
  out_char(1, '\n');
  return 0;
}

static int diag_pipeeof(int argc, char **argv) {
  (void)argc;
  (void)argv;
  int fds[2] = {-1, -1};
  if (pipe(fds) != 0) {
    out_str(2, "pipeeof: create failed\n");
    return 1;
  }
  if (set_handle_inherit(fds[0], 1) != 0) {
    out_str(2, "pipeeof: mark inherit failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  close(fds[1]);
  out_flush();
  long pid = spawn("/piperead");
  if (pid < 0) {
    out_str(2, "pipeeof: spawn failed\n");
    close(fds[0]);
    return 1;
  }
  int status = 0;
  long waited = wait_pid((int)pid, &status);
  close(fds[0]);
  if (waited < 0) {
    out_str(2, "pipeeof: wait failed\n");
    return 1;
  }
  out_str(1, "pipeeof: child ");
  out_u64(1, (unsigned long)pid);
  out_str(1, " status ");
  out_u64(1, (unsigned long)status);
  out_char(1, '\n');
  return 0;
}

static int diag_pipeblock(int argc, char **argv) {
  const char *arg = argc > 1 ? argv[1] : 0;
  int fds[2] = {-1, -1};
  const char *msg = (arg && *arg) ? arg : "delayed data";
  if (pipe(fds) != 0) {
    out_str(2, "pipeblock: create failed\n");
    return 1;
  }
  if (set_handle_inherit(fds[0], 1) != 0) {
    out_str(2, "pipeblock: mark inherit failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  out_flush();
  long pid = spawn("/piperead");
  if (pid < 0) {
    out_str(2, "pipeblock: spawn failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  out_flush();
  sleep_ms(50);
  if (write(fds[1], msg, strlen(msg)) < 0) {
    out_str(2, "pipeblock: write failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  int status = 0;
  long waited = wait_pid((int)pid, &status);
  close(fds[0]);
  close(fds[1]);
  if (waited < 0) {
    out_str(2, "pipeblock: wait failed\n");
    return 1;
  }
  out_str(1, "pipeblock: child ");
  out_u64(1, (unsigned long)pid);
  out_str(1, " status ");
  out_u64(1, (unsigned long)status);
  out_char(1, '\n');
  return 0;
}

static int diag_pipefull(int argc, char **argv) {
  (void)argc;
  (void)argv;
  int fds[2] = {-1, -1};
  char fill[512];
  long start = 0;
  long delta = 0;
  int status = 0;
  for (int i = 0; i < (int)sizeof(fill); ++i) {
    fill[i] = 'A';
  }
  if (pipe(fds) != 0) {
    out_str(2, "pipefull: create failed\n");
    return 1;
  }
  if (set_handle_inherit(fds[0], 1) != 0) {
    out_str(2, "pipefull: mark inherit failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  if (write(fds[1], fill, sizeof(fill)) != (long)sizeof(fill)) {
    out_str(2, "pipefull: initial fill failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  out_flush();
  long pid = spawn("/pipedelayread");
  if (pid < 0) {
    out_str(2, "pipefull: spawn failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  start = (long)time_ms();
  if (write(fds[1], "TAIL", 4) != 4) {
    out_str(2, "pipefull: tail write failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }
  delta = (long)time_ms() - start;
  close(fds[1]);
  if (wait_pid((int)pid, &status) < 0) {
    out_str(2, "pipefull: wait failed\n");
    close(fds[0]);
    return 1;
  }
  close(fds[0]);
  out_str(1, "pipefull: child ");
  out_u64(1, (unsigned long)pid);
  out_str(1, " status ");
  out_u64(1, (unsigned long)status);
  out_str(1, ", blocked ");
  out_u64(1, (unsigned long)delta);
  out_str(1, " ms\n");
  return 0;
}

static int diag_pipebroken(int argc, char **argv) {
  (void)argc;
  (void)argv;
  int fds[2] = {-1, -1};
  long rc = 0;
  if (pipe(fds) != 0) {
    out_str(2, "pipebroken: create failed\n");
    return 1;
  }
  close(fds[0]);
  rc = write(fds[1], "X", 1);
  close(fds[1]);
  if (rc >= 0) {
    out_str(2, "pipebroken: expected write failure\n");
    return 1;
  }
  out_str(1, "pipebroken: write rc ");
  out_u64(1, (unsigned long)(-rc));
  out_str(1, " (negative errno)\n");
  return 0;
}

static int diag_pollpipe(int argc, char **argv) {
  (void)argc;
  (void)argv;
  int fds[2] = {-1, -1};
  char byte = 0;
  long ready = 0;

  if (pipe(fds) != 0) {
    out_str(2, "pollpipe: create failed\n");
    return 1;
  }

  out_str(1, "pollpipe: reader initial ");
  ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
  write_poll_mask(ready);
  out_char(1, '\n');

  out_str(1, "pollpipe: writer initial ");
  ready = poll_fd(fds[1], POLL_WRITE | POLL_HUP);
  write_poll_mask(ready);
  out_char(1, '\n');

  if (write(fds[1], "x", 1) != 1) {
    out_str(2, "pollpipe: write failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }

  out_str(1, "pollpipe: reader after write ");
  ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
  write_poll_mask(ready);
  out_char(1, '\n');

  close(fds[1]);
  out_str(1, "pollpipe: reader after writer close ");
  ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
  write_poll_mask(ready);
  out_char(1, '\n');

  if (read(fds[0], &byte, 1) != 1) {
    out_str(2, "pollpipe: read failed\n");
    close(fds[0]);
    return 1;
  }
  out_str(1, "pollpipe: read byte ");
  out_char(1, byte);
  out_char(1, '\n');

  out_str(1, "pollpipe: reader after drain ");
  ready = poll_fd(fds[0], POLL_READ | POLL_HUP);
  write_poll_mask(ready);
  out_char(1, '\n');
  close(fds[0]);

  if (pipe(fds) != 0) {
    out_str(2, "pollpipe: second create failed\n");
    return 1;
  }
  close(fds[0]);
  out_str(1, "pollpipe: writer after reader close ");
  ready = poll_fd(fds[1], POLL_WRITE | POLL_HUP);
  write_poll_mask(ready);
  out_char(1, '\n');
  close(fds[1]);
  return 0;
}

static int diag_pollwaitpipe(int argc, char **argv) {
  (void)argc;
  (void)argv;
  int fds[2] = {-1, -1};
  long pid = 0;
  long ready = 0;
  unsigned long start = 0;
  unsigned long elapsed = 0;
  int status = 0;
  char byte = 0;

  if (pipe(fds) != 0) {
    out_str(2, "pollwaitpipe: create failed\n");
    return 1;
  }

  start = time_ms();
  ready = poll_wait_fd(fds[0], POLL_READ | POLL_HUP, 40);
  elapsed = time_ms() - start;
  out_str(1, "pollwaitpipe: timeout wait ");
  write_poll_mask(ready);
  out_str(1, " after ");
  out_u64(1, elapsed);
  out_str(1, " ms\n");

  out_flush();
  pid = spawn_with_stdio("/pollwakewriter", SPAWN_STDIO_NULL, fds[1],
                         SPAWN_STDIO_NULL);
  if (pid < 0) {
    out_str(2, "pollwaitpipe: spawn failed\n");
    close(fds[0]);
    close(fds[1]);
    return 1;
  }

  start = time_ms();
  ready = poll_wait_fd(fds[0], POLL_READ | POLL_HUP, 500);
  elapsed = time_ms() - start;
  out_str(1, "pollwaitpipe: wake wait ");
  write_poll_mask(ready);
  out_str(1, " after ");
  out_u64(1, elapsed);
  out_str(1, " ms\n");

  if ((ready & POLL_READ) != 0 && read(fds[0], &byte, 1) == 1) {
    out_str(1, "pollwaitpipe: read byte ");
    out_char(1, byte);
    out_char(1, '\n');
  }

  close(fds[1]);
  if (wait_pid((int)pid, &status) < 0) {
    out_str(2, "pollwaitpipe: wait failed\n");
    close(fds[0]);
    return 1;
  }

  start = time_ms();
  ready = poll_wait_fd(fds[0], POLL_READ | POLL_HUP, 100);
  elapsed = time_ms() - start;
  out_str(1, "pollwaitpipe: eof wait ");
  write_poll_mask(ready);
  out_str(1, " after ");
  out_u64(1, elapsed);
  out_str(1, " ms\n");

  close(fds[0]);
  return 0;
}

static int diag_pollmanypipe(int argc, char **argv) {
  (void)argc;
  (void)argv;
  int left[2] = {-1, -1};
  int right[2] = {-1, -1};
  poll_fd_t fds[2];
  long pid = 0;
  long ready = 0;
  int status = 0;
  char byte = 0;

  if (pipe(left) != 0 || pipe(right) != 0) {
    out_str(2, "pollmanypipe: create failed\n");
    if (left[0] >= 0) close(left[0]);
    if (left[1] >= 0) close(left[1]);
    if (right[0] >= 0) close(right[0]);
    if (right[1] >= 0) close(right[1]);
    return 1;
  }

  fds[0].fd = left[0];
  fds[0].events = POLL_READ | POLL_HUP;
  fds[0].revents = 0;
  fds[1].fd = right[0];
  fds[1].events = POLL_READ | POLL_HUP;
  fds[1].revents = 0;

  ready = poll_many_wait(fds, 2, 0);
  out_str(1, "pollmanypipe: snapshot ready ");
  out_u64(1, (unsigned long)ready);
  out_str(1, ", left ");
  write_poll_mask(fds[0].revents);
  out_str(1, ", right ");
  write_poll_mask(fds[1].revents);
  out_char(1, '\n');

  fds[0].revents = 0;
  fds[1].revents = 0;
  ready = poll_many_wait(fds, 2, 40);
  out_str(1, "pollmanypipe: timeout ready ");
  out_u64(1, (unsigned long)ready);
  out_str(1, ", left ");
  write_poll_mask(fds[0].revents);
  out_str(1, ", right ");
  write_poll_mask(fds[1].revents);
  out_char(1, '\n');

  out_flush();
  pid = spawn_with_stdio("/pollwakewriter", SPAWN_STDIO_NULL, right[1],
                         SPAWN_STDIO_NULL);
  if (pid < 0) {
    out_str(2, "pollmanypipe: spawn failed\n");
    close(left[0]);
    close(left[1]);
    close(right[0]);
    close(right[1]);
    return 1;
  }

  close(right[1]);
  fds[0].revents = 0;
  fds[1].revents = 0;
  ready = poll_many_wait(fds, 2, 500);
  out_str(1, "pollmanypipe: wake ready ");
  out_u64(1, (unsigned long)ready);
  out_str(1, ", left ");
  write_poll_mask(fds[0].revents);
  out_str(1, ", right ");
  write_poll_mask(fds[1].revents);
  out_char(1, '\n');

  if ((fds[1].revents & POLL_READ) != 0 && read(right[0], &byte, 1) == 1) {
    out_str(1, "pollmanypipe: read byte ");
    out_char(1, byte);
    out_char(1, '\n');
  }

  if (wait_pid((int)pid, &status) < 0) {
    out_str(2, "pollmanypipe: wait failed\n");
    close(left[0]);
    close(left[1]);
    close(right[0]);
    return 1;
  }

  close(left[1]);
  fds[0].revents = 0;
  fds[1].revents = 0;
  ready = poll_many_wait(fds, 2, 100);
  out_str(1, "pollmanypipe: eof ready ");
  out_u64(1, (unsigned long)ready);
  out_str(1, ", left ");
  write_poll_mask(fds[0].revents);
  out_str(1, ", right ");
  write_poll_mask(fds[1].revents);
  out_char(1, '\n');

  close(left[0]);
  close(right[0]);
  return 0;
}

typedef struct DiagEntry {
    const char *name;
    BuiltinFn fn;
} DiagEntry;

static const DiagEntry diagnostics[] = {
    {"pipe", diag_pipe},
    {"piperun", diag_piperun},
    {"pipenoinherit", diag_pipenoinherit},
    {"pipeeof", diag_pipeeof},
    {"pipeblock", diag_pipeblock},
    {"pipefull", diag_pipefull},
    {"pipebroken", diag_pipebroken},
    {"pollpipe", diag_pollpipe},
    {"pollwaitpipe", diag_pollwaitpipe},
    {"pollmanypipe", diag_pollmanypipe},
};

static int name_eq(const char *a, const char *b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

BuiltinFn diag_find(const char *name) {
    for (size_t i = 0; i < sizeof(diagnostics) / sizeof(diagnostics[0]); ++i) {
        if (name_eq(diagnostics[i].name, name))
            return diagnostics[i].fn;
    }
    return 0;
}
