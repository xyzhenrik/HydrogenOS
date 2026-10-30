# Performance counters

The kernel keeps a fixed set of counters (`kernel/core/perf.{c,h}`) that cost a
few instructions on the hot paths. They are read through `sys_perf_read` (81)
and, rendered as text, through `/proc/stat` and `/proc/interrupts`
(`docs/PROCFS.md`). `top` and `perfstat` are the userspace front ends.

## Design

- Counters live in one `perf_cpu_t` per CPU and are plain 64-bit increments:
  kernel code runs with interrupts disabled (`syscall` masks IF, interrupt
  gates clear it, the idle `sti; hlt; cli` window touches no counter) and a CPU
  only writes its own block. There are no atomics and no locks; a snapshot sums
  the blocks. Only `perf_local()` has to change when a second CPU is started.
- Time is measured in TSC ticks. Every moment since boot is attributed to
  exactly one of user, kernel or idle: `isr_handler` brackets each trap
  (`perf_trap_enter` / `perf_trap_exit`), the scheduler splits off the part that
  belongs to the process that was running, and the idle loop accounts the
  halted time. The time between two snapshots is therefore
  `user + kernel + idle` up to the one syscall in flight at each end.
- Per process the kernel keeps `user_cycles` and `kernel_cycles`
  (`/proc/<pid>/stat`). Kernel cycles are charged to the process whose syscall,
  interrupt or scheduling decision consumed them; a process that exits takes its
  numbers with it, the system-wide totals keep them.
- `tsc_khz` is not calibrated against hardware: it is the TSC advance since
  boot divided by the timer-tick uptime, and 0 during the first 100 ms. Shares
  never need it; it only converts cycles to time for display.

## Counters (`hydrogen_perf_counters_t`)

| Field | Meaning |
|-------|---------|
| `user_cycles`, `kernel_cycles`, `idle_cycles` | TSC ticks per mode; kernel includes the boot sequence before the first process |
| `context_switches` | the CPU was handed to a different process than the one that was running |
| `wakeups` | a blocked process (including one interrupted by a signal) was dispatched again |
| `idle_entries` | `hlt` executions with nothing to run |
| `pipe_bytes_read`, `pipe_bytes_written` | bytes moved by `kernel_pipe_read` / `kernel_pipe_write` |
| `net_rx_packets`, `net_tx_packets`, `net_rx_bytes`, `net_tx_bytes` | frames handed to or accepted from the NIC |
| `syscall_calls[96]`, `syscall_cycles[96]` | calls and cumulative cycles from trap entry to the end of the handler, indexed by syscall number; slot 95 collects every number >= 95. Blocking syscalls return immediately, so waiting time is not included |
| `irq_counts[256]` | traps per vector: exceptions (page faults are vector 14), IRQs from 32, the syscall entry at 128 |

## `sys_perf_read(buf, size)`

Copies `hydrogen_perf_snapshot_t` (`kernel/include/perf_abi.h`, mirrored in
`userspace/lib/perf.h`) to `buf`, truncated to `size` bytes, and returns the
number of bytes copied. The header carries `version` (currently 1), `size`
(the kernel's struct size), the table lengths, `tsc`, `tsc_khz` and
`uptime_ms`. New counters are appended at the end of
`hydrogen_perf_counters_t` and bump the version; a caller that wants to be
forward compatible checks `version` and `size` before trusting the tail.
`-EINVAL` for `size < 8`, `-EFAULT` for an unusable buffer.

Rates and shares come from the difference of two snapshots; `tsc` is the time
base.

## Userspace

- `top [-d seconds] [-n iterations] [-b]` shows CPU shares per process for
  the last interval (default 2 s, rows add up to the busy share in the
  header). `q` quits; `-b` prints frames without clearing the screen.
- `perfstat` prints the counters since boot; `perfstat <command> [args]` runs a
  command and prints what the machine did while it ran.
- `/perftest` is the boot test (`PERF PASS`): interface checks, then a workload
  of syscalls, a pipe ping-pong with a helper process, a CPU burn, a sleep and
  a crashing child, with lower bounds on every counter the workload touches and
  a check that user, kernel and idle cycles cover the elapsed TSC time.
