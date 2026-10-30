# /proc — synthetic process and kernel information

`procfs` (`kernel/fs/procfs.{c,h}`) is a filesystem that the VFS
mounts at `/proc` during `vfs_init`, right after the root filesystem.
It has no backing store: every `read`/`stat` renders the file from live
kernel state. Offsets are honoured against that fresh rendering, so a reader
that needs a consistent snapshot should fetch the file with one large read
(all files fit in 2 KiB).

`/proc/boot` accepts progress reports for the boot splash through
`SYS_WRITEF` from init only (see `docs/BOOT_SPLASH.md`). Every other write,
and `mkdir`, `unlink` and `rename` below `/proc`, fail (`-EROFS` for files,
`-ENOENT` for unknown paths).

## Layout

| Path                 | Type | Contents                                   |
|----------------------|------|--------------------------------------------|
| `/proc/meminfo`      | file | physical memory and kernel heap usage      |
| `/proc/uptime`       | file | seconds since boot, `"<s>.<cc>\n"`         |
| `/proc/version`      | file | `"HydrogenOS <version> <arch>\n"`          |
| `/proc/dcache`       | file | directory entry cache counters (`docs/DCACHE.md`) |
| `/proc/stat`         | file | system-wide performance counters           |
| `/proc/interrupts`   | file | per-vector interrupt counts                |
| `/proc/boot`         | file | boot splash state and progress             |
| `/proc/cmdline`      | file | kernel command line, one line              |
| `/proc/<pid>/`       | dir  | one per live process, listed in pid order  |
| `/proc/<pid>/status` | file | per-process status                         |
| `/proc/<pid>/stat`   | file | per-process CPU accounting                 |

Only canonical decimal pids resolve (`/proc/07` does not). Processes that
have exited are not listed, even while their exit status is still unreaped.

## File formats

All files use `Label:` padded to 16 columns, followed by the value and an
optional unit, one field per line. Consumers should look fields up by label,
not by line number; new fields may be appended.

`/proc/<pid>/status`:

| Field      | Meaning                                                        |
|------------|----------------------------------------------------------------|
| `Name`     | basename of the executable the process was spawned from        |
| `Pid`      | process id                                                     |
| `PPid`     | parent process id (0 for the first user process)               |
| `State`    | `running` (the reader itself), `runnable`, `wait`, `pipe-read`, `pipe-write`, `poll`, `sleeping` |
| `Priority` | `idle`, `normal`, `high`, `rt`                                 |
| `Handles`  | used handle-table slots plus bound stdio slots                 |
| `Pages`    | 4 KiB user frames owned by the address space (tables excluded) |
| `Cwd`      | current working directory                                      |

`/proc/meminfo`: `MemTotal`, `MemFree`, `MemUsed` (kB), `PageSize` (B),
`PagesTotal`, `PagesFree`, and kernel heap `HeapTotal`, `HeapUsed`,
`HeapFree`, `HeapLargestFree` (B; block headers excluded, so
`HeapUsed + HeapFree == HeapTotal`).

`/proc/<pid>/stat` (changes on every read, hence separate from `status`):

| Field          | Meaning                                                  |
|----------------|----------------------------------------------------------|
| `Name`, `Pid`, `State` | as in `status`                                   |
| `Tsc`          | TSC at render time; the time base for the two below      |
| `UserCycles`   | TSC ticks the process spent in user mode                 |
| `KernelCycles` | TSC ticks the kernel spent working for it (syscalls, its interrupts, scheduling) |

`/proc/stat` mirrors the counters of `sys_perf_read` (see `docs/PERF.md`):
`Tsc`, `TscKHz`, `CpuUser`, `CpuKernel`, `CpuIdle`, `CtxSwitches`,
`Wakeups`, `IdleEntries`, `Syscalls`, `Interrupts`, `PageFaults`,
`PipeReadBytes`, `PipeWriteBytes`, `NetRxPackets`, `NetTxPackets`,
`NetRxBytes`, `NetTxBytes`.

`/proc/interrupts` has one line `<vector>: <count> <name>` per vector that has
fired, counts right-aligned, e.g. `  32:        10231 timer`. Vector 128 is
the syscall entry; vectors below 32 are CPU exceptions.

`/proc/boot`: `Splash` (`active`, `finished` or `disabled`) and `Progress`
(0 to 1000, per mille of the boot sequence).

## Kernel interface

procfs reads the process table only through the snapshot accessors in
`kernel/core/process.h` (`process_get_info`, `process_next_info`), which copy
into a `process_info_t` and never hand out `process_t` pointers.

## Userspace

- `/ps` prints one line per process from `/proc/<pid>/status`.
- `/top` refreshes a CPU-share table from `/proc/<pid>/stat`, `/perfstat`
  reports the performance counters (docs/PERF.md).
- `/proctest` is the boot test (`PROC PASS`): it checks its own status,
  chunked and seeked reads, `meminfo`, `uptime`, `version`, the `/proc`
  listing and rejection of invalid paths and writes.
