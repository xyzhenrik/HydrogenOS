# init.rc

`/init.rc` is read by `init` (pid 1) at boot. If it is missing, unreadable or
fails to parse, `init` falls back to the built-in configuration in
`userspace/init/init.c`. The source of the shipped file is
`userspace/init/init.rc`.

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
| `TYPE` | `oneshot`: run to completion before the next service starts. `supervised`: like `oneshot`, but defaults to `restart=always`. `daemon`: spawned once, `init` neither waits for nor restarts it, so the restart options have no effect. |
| `PATH` | Executable path (max 63 characters). |
| `DELAY_MS` | Base delay in milliseconds before a restart or retry. |
| `DEPENDENCY` | Name of an earlier service. The service only starts if every dependency completed successfully (a daemon counts as successful once it has been spawned). |

Options and dependencies may be mixed in any order after `DELAY_MS`.

| Option | Meaning |
|--------|---------|
| `restart=never\|on-failure\|always` | Restart policy. Default: `on-failure` for `oneshot`, `always` for `supervised` and `daemon`. |
| `retries=N` | Maximum number of consecutive retries after a failed run (0 to 16, default 3). |
| `stdin=inherit\|null\|pipe "TEXT"` | `pipe` feeds `TEXT` to the service. |
| `stdout=inherit\|null\|capture "TEXT"` | `capture` requires the service to print exactly `TEXT`, otherwise the run counts as failed. |
| `stderr=...` | Same modes as `stdout`. |
| `stdin=channel NAME`, `stdout=channel NAME`, `stderr=channel NAME` | Connect to a named pipe shared by several services. The write end is closed after the last writer has been started or skipped, so the reader sees end-of-file. |
| `stdin=tee NAME`, `stdout=tee NAME`, `stderr=tee NAME` | Collect output into a named buffer and replay it to a reader. |
| `display=owner` | Marks a `daemon` that takes over the screen by presenting full frames. Without one, init removes the boot splash when startup ends (see Boot progress). |
| `stdout=logsink NAME`, `stderr=logsink NAME` | Collect output into a log sink declared with `logsink`. |

A run counts as failed if the service exits with a non-zero status, cannot be
spawned, or its captured output does not match.

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

## Example

```
service net oneshot /netup 100 retries=5
service sync oneshot /sync 100 restart=on-failure retries=1 net
```

`net` is retried up to five times with delays of 100, 200, 400, 800 and
1600 ms. If it still fails, `sync` is skipped.
