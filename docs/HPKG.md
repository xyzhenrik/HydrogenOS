# hpkg: packages, package manager, repository

`hpkg` (`/bin/hpkg`) installs and removes software packages (`.hpkg` files),
tracks which package owns which file, and installs packages by name from a
local repository. It is a pure userspace program on top of the existing
syscalls; checksums are SHA-256 (`userspace/lib/sha256.c`).

## Commands

```
hpkg [--root DIR] [--repo DIR] <command> [arguments]

install <file.hpkg | name>...   install from a file, or by name from the repository
remove <name>...                remove packages
list                            installed packages: "<name> <version>"
info <name>                     details, file list, reverse dependencies
verify [name...]                compare installed files with the database
update                          validate the repository, show available upgrades
```

An `install` argument that contains a `/` or ends in `.hpkg` is a file; any
other argument is a package name looked up in the repository. Exit status:
0 success, 1 failure (diagnostics on stderr), 2 usage.

`--root DIR` relocates the database and every installed file below `DIR`
(the directory must exist; at most 32 characters). `--repo DIR` selects another
repository; the default is `<root>/var/lib/hpkg/repo`. `tools/uitest`-style
manual tests and `hpkgtest` use both options so they never touch the system.

Behaviour worth knowing:

- Installing a version that is already installed does nothing (exit 0).
  Installing a newer version upgrades; an older one is refused.
- `install <name>` resolves dependencies recursively from the repository and
  installs them first. If any package of that run fails, the dependencies the
  run had added are removed again.
- `remove` refuses while another installed package depends on the package.
- `update` checks every package file in the repository against the index
  (size and SHA-256) and lists installed packages with a newer version
  available. The repository is a directory on the local disk, so there is
  nothing to download; `update` is the integrity check and upgrade overview.
- Dependencies are plain package names without version constraints.

## Safe installation

`hpkg_store_install` is transactional:

1. The archive is read completely and verified (see below) before anything
   on the target is touched.
2. Dependencies must be installed. A file of the package must not exist
   already unless the package being upgraded owns it, and no other package
   may own it.
3. Missing directories are created (and remembered), every file is written
   as `<path>.hpkg-new`, and permissions are applied.
4. Each file is moved into place with `rename`; a file of the previous
   version is first moved to `<path>.hpkg-old`. Files the new version no
   longer ships are moved aside the same way.
5. The database record is written to `<name>.hpkg-new` and renamed over the
   old record. This is the commit point.
6. The `.hpkg-old` files are deleted.

A failure in steps 3 to 5 runs the rollback: committed files are removed,
moved-aside files are renamed back, staged files are deleted, directories the
transaction created are removed. The previous version stays installed and
intact. The names `*.hpkg-new` and `*.hpkg-old` are reserved; packages may not
contain them. A crash in the middle can leave such files behind, but never a
half-written file under its real name and never a record for a package that
was not completely installed.

Limitations: `remove` deletes files even if they were modified (`verify`
reports modifications beforehand); there is no configuration-file handling;
directories shared by several packages are removed together with the package
that happened to create them only when they are empty, so a shared directory
can outlive all of its users.

## Package archive format (`.hpkg`)

All integers are little-endian `uint32`.

```
"HPK1"                                  magic
u32 manifest_length, manifest bytes     text, see below
u32 file_count
file_count times:
    u32 path_length, path bytes         absolute install path
    u32 mode                            permission bits only (0..0777)
    u32 size
    u8[32] sha256 of the file data
    data
u8[32] sha256 of everything before it
```

The manifest is ASCII text, one `key value` pair per line: `name`,
`version`, `description` (optional), `depends` (optional, space separated
names). Unknown keys are ignored.

- Names: lower-case `[a-z0-9+_-]`, start with a letter or digit, at most 32
  characters. Versions: `[A-Za-z0-9.+~_-]`, at most 32 characters, compared
  segment by segment (numbers numerically).
- Paths are absolute and canonical: no `//`, `.` or `..` components, no
  trailing slash, printable ASCII without blanks, at most 200 characters, and
  not inside `/var/lib/hpkg`. Directories are implicit.
- The file table is sorted: strictly ascending when `/` is treated as the
  smallest character, so a directory is immediately followed by its content.
  This makes duplicates and "file used as directory" detectable in one
  linear pass.
- Limits: 4096 files, 4096 manifest bytes, 32 MiB per archive.

The reader rejects archives with a wrong magic, a bad trailer or file
checksum, truncated or trailing data, unsafe paths, or an unsorted table.

## Database

`/var/lib/hpkg/db/<name>` is a text file per installed package:

```
name greeter
version 1.2.0
description Prints a greeting in several languages
depends libgreet
file <sha256> <size> <octal mode> <path>      one per owned file, sorted
dir <path>                                    directories this install created
```

The `file` lines are the ownership table: a path appears in at most one
record. `verify` re-hashes each file and also compares size and permissions.

## Repository

A repository is a directory containing `*.hpkg` files and an `index`:

```
HPKG-INDEX 1
<name> <version> <size> <sha256> <deps> <file> [description]
```

`<size>` and `<sha256>` describe the `.hpkg` file as a whole, `<deps>` is a
comma separated name list or `-`. There is one entry per package name. Before
using a file `hpkg` checks size and SHA-256 against the index and the
manifest's name and version against the entry, then verifies the archive
itself.

The system repository is `/var/lib/hpkg/repo`. It is built into the disk
image from the sample sources in `packages/` (`libgreet`, `greeter` which
depends on it, and `fortune-lite`).

## Building packages and repositories

`tools/hpkg_repo.py` runs on the host:

```
tools/hpkg_repo.py pack <source-dir> <out.hpkg>
tools/hpkg_repo.py index <repository-dir>
tools/hpkg_repo.py build <packages-dir> <repository-dir>
```

A source directory has a `manifest` file and the files to install under
`root/` (the path below `root/` is the install path; executable files get mode
0755, everything else 0644). `index` reads every `.hpkg` in the directory,
validates it with the same rules as the target and writes `index`. `build`
packs each subdirectory of `packages/` and indexes the result; the Makefile
runs it and `tools/stage_rootfs.py --repo` copies the result into the image.

## Test

`/lib/tests/hpkgtest` (started from `init.rc`, prints `HPKG PASS`, required by
`tools/boot_smoke.sh`) drives the real `/bin/hpkg` against a scratch root in
`/tmp`: SHA-256 test vectors, install/list/info/verify, dependency refusal,
conflicts with owned and unowned files, unsafe and corrupted archives,
failure halfway through an installation (rollback), upgrade, downgrade
refusal, removal with directory cleanup, tamper detection, and installation
from the system repository plus a damaged repository whose dependency
installation must be undone.
