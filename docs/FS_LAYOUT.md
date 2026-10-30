# Root filesystem layout

```
/
├── bin/          user commands: shell cat echo ps ping nslookup date time
│                 sleep hello logread poweroff reboot wc free crashinfo hpkg
├── sbin/         system programs: init syslogd login compositor desktopdemo crashcollect
├── lib/
│   └── tests/    boot self-tests (*test, *check) and the helper programs
│                 they spawn (piperead, pollwakewriter, logproducer_a, ...)
├── etc/          init.rc passwd settings.conf
├── home/user/    the default user's home
├── root/         home of the root account
├── tmp/          scratch space, also used by the self-tests (mode 1777)
├── usr/share/    architecture-independent data (packages, resources)
├── var/log/      system.log written by syslogd
├── var/crash/    crash-N.txt dumps collected at boot (CRASH_OOM.md)
├── var/lib/hpkg/ package database (db/) and package repository (repo/), see HPKG.md
└── proc/         procfs mount point (see PROCFS.md)
```

Nothing but these directories lives in `/`. Programs never install
themselves next to each other in the root; a new binary needs a line in
`tools/rootfs.manifest`.

## Where things are decided

| What | Where |
|------|-------|
| Which program goes into which directory | `tools/rootfs.manifest` |
| Static files (`/etc/passwd`, `/etc/settings.conf`, `/home/user/...`) | `fsroot/` |
| Boot services and their binary paths | `userspace/init/init.rc` (installed as `/etc/init.rc`) |
| Kernel entry point | `/sbin/init`; if it is missing the kernel tries `/bin/shell` |
| Shell command lookup | `$PATH`, default `/bin:/sbin` |
| Syslog | `/var/log/system.log` and `/var/log/system.log.1` |
| Crash dumps | `/var/crash/crash-N.txt` |

The manifest format is one `<directory> <program> [<program> ...]` line per
group. A program `name` is the binary built as `build/<arch>/user_name.elf`
and is installed as `/<directory>/name`. The Makefile stops with an error if a
userspace binary exists that the manifest does not place, so a new program can
never silently go missing from the image.

## How the image is built

`tools/stage_rootfs.py` assembles one staging tree in `build/<arch>/rootfs`:
it copies `fsroot/`, creates the standard directories (including empty ones
like `/root` and `/proc`), places the programs from the manifest, copies the
built package repository to `/var/lib/hpkg/repo` and installs `init.rc` as
`/etc/init.rc`. Both boot media pack that same tree:

- **Disk image** (`build/<arch>/disk.img`): `mke2fs -d` turns the staging tree
  into the ext2 root. This is the normal root filesystem.
- **Initrd** (`build/<arch>/initrd.cpio`): `tools/mkinitrd.py` writes the same
  tree as a cpio archive with full paths (`sbin/init`, `bin/cat`, ...) and an
  explicit entry for every directory. The kernel uses it as a read-only RAMFS
  root when no disk is attached; RAMFS resolves nested paths and lists
  directories from the archive, so both roots have the same layout.

`/proc` is mounted by the VFS on top of whichever root was found.

## Conventions for programs

- Test scratch files go to `/tmp`, never to `/`. A test removes what it
  created.
- Tests that spawn a sibling binary use its absolute path under
  `/lib/tests` (or `/bin` for ordinary commands).
- Desktop `run NAME` and the shell resolve a bare command name against
  `/bin` and then `/sbin`; a name containing `/` is used as given.
