# Crash dumps and the out-of-memory policy

Two mechanisms for when things go wrong: the kernel keeps a report of what it
was doing when it crashed, and it kills a process instead of failing the whole
system when memory runs out. Syscalls: `sys_crash` (82) and `sys_meminfo` (83),
documented in `docs/SYSCALL_ABI.md`.

## Crash dumps

### What triggers one

| Kind | When | Kernel keeps running |
|------|------|----------------------|
| `panic` | `PANIC()` / `ASSERT()` and unrecoverable CPU exceptions in kernel mode | no |
| `oops` | `sys_crash(TEST)`, a simulated oops used by `crashtest` | yes |
| `user fault` | a process dies of `SIGSEGV`, `SIGILL`, `SIGFPE`, `SIGBUS` or `SIGTRAP` caused by a CPU fault | yes |

A panic or oops always replaces whatever dump is waiting. A user fault only
records a dump if none is waiting, so the first event of a boot is the one that
survives (the same fault in a loop must not bury the cause).

### Contents

`kernel/core/crash.c` renders a text report into a 16 KiB buffer:

- header: kind, reason, kernel version, uptime, current process;
- registers of the trapped context (for a panic raised by a CPU exception, the
  faulting frame; for a user fault, the process's frame);
- backtrace: the frame-pointer chain, `#0` being the faulting instruction.
  The kernel is built with `-fno-omit-frame-pointer`. Every frame address is
  checked (alignment, canonical, mapped, climbing) before it is read, so a
  damaged stack ends the trace and never the report. Addresses are raw;
  resolve them with `addr2line -e build/x86_64/kernel.bin` or
  `nm -n`. User backtraces only go as far as the program keeps frame pointers;
- process table (pid, parent, state, priority, user pages, name; `*` marks the
  current process);
- the last 24 lines of console output (`kernel/core/kmsg.c`, a ring fed by
  `serial_putc`). syslogd's lines reach the console like all output, so they
  appear here; kernel messages such as the OOM killer's do too.

### Where it is stored

The image build appends one MiB behind the 16 MiB ext2 filesystem
(`DISK_SIZE_WITH_CRASH_REGION` in the Makefile). The last 64 sectors of the
boot disk are the crash region: sector 0 is a header (magic, version, length,
FNV-1a checksum), the dump follows. `crash_init()` uses the region only if the
mounted filesystem (`ext2_info`) ends before it; on other layouts (no disk,
ATA read-only, filesystem filling the disk) the dump stays in memory and is
lost on reboot.

Writing at crash time goes straight to the device (`block_write`, polled
virtio, no block cache, no filesystem code): data first, flush, then header,
flush. A header that passes its checks therefore always describes a complete
dump. The write allocates two small virtio request buffers from the kernel
heap; a panic inside the heap allocator itself can lose the dump.

### Collecting

`/sbin/crashcollect` runs from `init.rc` right after `syslogd`. It calls
`crash_collect()` (libc, `userspace/lib/crash.c`): `sys_crash(READ)` fetches
the dump from the disk region, it is written as the next free
`/var/crash/crash-N.txt`, then `sys_crash(CLEAR)` zeroes the header. The clear
waits 1.5 s after the file write so the block cache has written the file back
first; a crash in that window keeps the disk copy and the next boot collects
it again.

`crashinfo` lists the dumps, `crashinfo N` prints one, `crashinfo --collect`
collects a pending dump by hand. `sys_crash` is root-only because a dump
contains kernel addresses and the process list.

### Test

`/lib/tests/crashtest` (`CRASH PASS`): drains leftovers, checks an unprivileged
caller gets `-EPERM`, kills a child with a null-page store (user fault dump),
triggers `sys_crash(TEST)` (oops dump), collects both through the real path and
checks the sections of each file, then checks the kernel copy is gone.

## Out-of-memory policy

### Entry points

`kernel/mm/oom.c` wraps the two allocators on behalf of user processes:
`oom_alloc_page()` (user pages for `sys_mmap` and the ELF loader, page-table
pages) and `oom_kalloc()` (mapping descriptors, process objects). When the
allocation fails, the policy picks a victim, kills it, and retries once. If
that still fails the caller sees the usual `-ENOMEM`. `sys_mmap` no longer
refuses a request merely because fewer pages are free than requested; only a
request larger than all managed memory is refused up front.

### Choosing the victim

Score of a process = resident user pages x (4 - priority), with priority
`idle` 0 to `rt` 3, so of two equal processes the less important dies first. The
highest score wins. Candidates are widened in three passes, each used only if
the previous one found nobody:

1. everything except init, the caller's session leader and processes with
   `high` or `rt` priority (system services);
2. everything except init and the session leader;
3. everything except init.

The session leader is the caller's ancestor directly below init (the caller
itself if init is its parent), i.e. the root of the tree the allocation belongs
to. The caller can be the victim.

### Killing

`signal_terminate(victim, SIGKILL)` ends the process at once (exit status
`SIGNAL_STATUS(SIGKILL)`, parent notified). If the victim is another process,
its address space and kernel objects are released immediately
(`process_destroy`) so the retry can use them. If the victim is the caller
itself it cannot be destroyed while running; it is marked dead, the allocation
fails, and the scheduler reaps it when the syscall returns. The kill is logged
to the console and serial port as `oom: killing pid N (name) rss=... pages
score=...` and counted in `oom_kills`.

### Reporting

`sys_meminfo` returns total/free pages, kernel heap total/used and the OOM kill
count (`hydrogen_meminfo_t`, `kernel/include/meminfo_abi.h`). The tool `free`
prints it; `/proc/meminfo` has the same count as `OomKills`.

### Test

`/lib/tests/oomtest` (`OOM PASS`) runs after all other tests, so nothing but
init and the daemons exist: it starts three hogs that map 1 MiB at a time until
refused, waits for them, and checks that at least one was killed by `SIGKILL`,
that the number of kernel kills equals the number of dead hogs, that the free
page count recovered, that init still answers `kill(1, 0)` and that the test
itself can still allocate.

## Limits

- The OOM kill line goes to the console and serial port (and thus the crash
  dump's log), not into `/var/log/system.log`: syslogd only reads pipes, and
  the kernel has no path to it.
- Page-table and kernel-heap exhaustion by processes that are not user mappings
  (sockets, pipes) are not routed through the OOM policy.
- There is a single crash slot; only one dump can wait at a time.
