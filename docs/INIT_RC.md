# init.rc

`/etc/init.rc` is read by `init` (`/sbin/init`, pid 1) at boot. If it is
missing, unreadable or fails to parse, `init` falls back to the built-in
configuration in `userspace/init/init.c`. The source of the shipped file is
`userspace/init/init.rc`; the image build installs it as `/etc/init.rc` (see
[FS_LAYOUT.md](FS_LAYOUT.md)).

Lines are processed in order. Empty lines and everything after `#` are
ignored. Tokens are separated by whitespace; a token in double quotes may
contain spaces and the escapes `\n`, `\t`, `\\` and `\"`. At most 48 services
are supported.

## Directives

### service

```
service NAME TYPE PATH DELAY_MS [option ...] [DEPENDENCY ...]
```

| Field | Meaning |
|-------|---------|
| `NAME` | Unique service name (max 31 characters). |
| `TYPE` | `oneshot`: run to completion before the next service starts. `supervised`: like `oneshot`, but defaults to `restart=always`. `daemon`: started without waiting for it; `init` supervises it in the background and restarts it according to its restart policy (see Supervision). |
| `PATH` | Absolute executable path (max 63 characters): system services live in `/sbin`, boot self-tests in `/lib/tests`. |
| `DELAY_MS` | Base delay in milliseconds before a restart or retry. |
| `DEPENDENCY` | Name of an earlier service. The service only starts if every dependency completed successfully (a daemon counts as successful once it has been spawned). |

Options and dependencies may be mixed in any order after `DELAY_MS`.

| Option | Meaning |
|--------|---------|
| `restart=never\|on-failure\|always` | Restart policy. Default: `on-failure` for `oneshot`, `always` for `supervised` and `daemon`. |
| `retries=N` | Maximum number of consecutive retries after a failed run (0 to 16, default 3). |
| `timeout=MS` | Longest time a `oneshot` may run before `init` kills it with `SIGKILL` and counts the run as failed (0 to 86400000). `0` disables the limit. Default: 120000 for `oneshot`, no limit for `supervised` and `daemon`, where the option has no effect. |
| `stdin=inherit\|null\|pipe "TEXT"` | `pipe` feeds `TEXT` to the service. |
| `stdout=inherit\|null\|capture "TEXT"` | `capture` requires the service to print exactly `TEXT`, otherwise the run counts as failed. |
| `stderr=...` | Same modes as `stdout`. |
| `stdin=channel NAME`, `stdout=channel NAME`, `stderr=channel NAME` | Connect to a named pipe shared by several services. The write end is closed after the last writer has been started or skipped, so the reader sees end-of-file. |
| `stdin=tee NAME`, `stdout=tee NAME`, `stderr=tee NAME` | Collect output into a named buffer and replay it to a reader. |
| `display=owner` | Marks a `daemon` that takes over the screen by presenting full frames. Without one, init removes the boot splash when startup ends (see Boot progress). |
| `stdout=logsink NAME`, `stderr=logsink NAME` | Collect output into a log sink declared with `logsink`. |

A run counts as failed if the service exits with a non-zero status (this
includes death by a signal), cannot be spawned, exceeds its `timeout`, or its
captured output does not match.

### logsink

```
logsink NAME "EXPECTED"
```

Declares a log sink. Once all of its writers have finished, the collected
output must equal `EXPECTED`.

## Failure handling

With `restart=on-failure` (and `restart=always`), a failed run is retried at
most `retries` times. Attempt `k` of the retries waits `DELAY_MS * 2^(k-1)`
milliseconds, capped at 30 seconds. A successful run of an `always` service
resets the failure counter. When the retries are used up, `init` logs

```
init: NAME giving up after N failed attempts
init: NAME failed permanently
```

and marks the service as failed. Boot continues with the remaining services.
Every service that depends on a failed or skipped service is skipped, which
makes the skip transitive:

```
init: NAME skipped, dependency DEP did not start
```

At the end of startup `init` logs `init: N service(s) failed or skipped` if
anything did not start. Daemons that did start keep running; if no daemon is
running, `init` exits with status 1 after a failure.

## Boot progress

While the kernel's boot splash is up, `init` reports its progress by writing
`services DONE TOTAL` to `/proc/boot` before it handles each service, and
`services TOTAL TOTAL` once the table is processed. If no daemon marked
`display=owner` was started, it then writes `end`, which removes the splash so
that the console is visible; failed services alone do not. A display owner
that has not replaced the splash within 30 seconds after startup is also
answered with `end`. See `docs/BOOT_SPLASH.md`.

## Start timeout

While a `oneshot` runs, `init` waits for it without blocking in the kernel.
When the service is still running after `timeout` milliseconds, `init` logs

```
init: NAME timed out after N ms, killed
```

sends it `SIGKILL`, reaps it and handles the run like any other failure:
`restart=on-failure` retries it, otherwise it is marked as failed and its
dependents are skipped. Boot continues; one hanging service delays it by at
most its timeout.

## Supervision

`init` supervises daemons from the moment they are spawned, also while it is
waiting for a `oneshot` or sleeping before a retry. It polls every 10 ms and
never blocks on a single service, so other services keep starting and other
daemons are restarted on time.

When a daemon exits, `init` logs `init: NAME exited with status N` and applies
its restart policy:

| Policy | Exit status 0 | Failure |
|--------|---------------|---------|
| `never` | stopped | stopped, counted as failed |
| `on-failure` | stopped | restarted after backoff |
| `always` | restarted after `DELAY_MS` | restarted after backoff |

A failure is a non-zero status, death by a signal or a failed respawn. Failures
use the retry budget and backoff described above (`retry K/N in D ms`, then
`restarted as daemon`). A clean exit under `always` resets the failure counter.
So does a run that lasted at least 10 seconds, so a daemon that is stable most
of the time is never given up on. Once the retries are used up, `init` logs
`giving up` and `failed permanently` and leaves the daemon stopped.

`init` keeps running as long as one daemon is running or waiting for a restart.
When the last daemon has stopped, `init` exits with status 1 if any service or
daemon failed, otherwise 0.

Restrictions that keep restarts safe:

- The `log=server` daemon is never restarted: the log pipes of the other
  services are bound to the instance that `init` started first.
- A daemon with a restart policy other than `never` cannot use `channel`,
  `tee` or `logsink` streams, because those are closed once their last writer
  has been started. The configuration is rejected.
- A restartable daemon that logs to `syslog` keeps its pipe: `init` holds the
  write end until the daemon is stopped, so `syslogd` does not see end-of-file
  between two instances.

`init` polls instead of reacting to `SIGCHLD`: `SYS_SIGPROCMASK` cannot wait
and unblock in one step, so a handler would leave a window in which an exit is
missed until the next wake-up anyway.

## Example

```
service net oneshot /sbin/netup 100 retries=5
service sync oneshot /sbin/sync 100 restart=on-failure retries=1 net
```

`net` is retried up to five times with delays of 100, 200, 400, 800 and
1600 ms. If it still fails, `sync` is skipped.

```
service syslogd daemon /sbin/syslogd 100 log=server
service netd daemon /netd 200 restart=on-failure retries=5
service migrate oneshot /migrate 100 restart=never timeout=5000
```

`netd` is restarted after 200, 400, 800, 1600 and 3200 ms if it keeps failing,
and `migrate` is killed if it runs longer than five seconds.
