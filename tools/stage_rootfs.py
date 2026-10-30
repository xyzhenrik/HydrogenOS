#!/usr/bin/env python3
"""Assemble the root filesystem tree that both the disk image and the initrd
are built from. See docs/FS_LAYOUT.md."""
import argparse
import os
import shutil
import sys

# Directories every installation has, whether or not anything is shipped in them.
STANDARD_DIRS = [
    "bin", "sbin", "lib/tests", "etc", "tmp", "var/log",
    "home/user", "root", "usr/share", "proc", "var/lib/hpkg/db",
]
DIRECTORY_MODES = {"tmp": 0o1777}


def read_manifest(path):
    """Returns (directory, program) pairs from the manifest."""
    entries = []
    with open(path) as manifest:
        for number, line in enumerate(manifest, 1):
            fields = line.split("#", 1)[0].split()
            if not fields:
                continue
            if len(fields) < 2:
                raise SystemExit(f"{path}:{number}: directory without programs")
            entries.extend((fields[0], program) for program in fields[1:])
    return entries


def normalize_modes(root):
    """Makes the permissions independent of the build user's umask and of the
    modes in the source tree: directories 0755 (except those in
    DIRECTORY_MODES), programs 0755, everything else 0644."""
    for current, dirs, files in os.walk(root):
        for name in dirs:
            path = os.path.join(current, name)
            relative = os.path.relpath(path, root)
            os.chmod(path, DIRECTORY_MODES.get(relative, 0o755))
        for name in files:
            path = os.path.join(current, name)
            executable = os.stat(path).st_mode & 0o100
            os.chmod(path, 0o755 if executable else 0o644)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="staging directory to create")
    parser.add_argument("--static", help="tree of static files copied to /")
    parser.add_argument("--manifest", help="program placement list")
    parser.add_argument("--build-dir", help="directory holding user_<program>.elf")
    parser.add_argument("--init-rc", help="file installed as /etc/init.rc")
    parser.add_argument("--repo", help="package repository copied to /var/lib/hpkg/repo")
    args = parser.parse_args()

    if args.manifest and not args.build_dir:
        parser.error("--manifest needs --build-dir")

    shutil.rmtree(args.out, ignore_errors=True)
    if args.static and os.path.isdir(args.static):
        shutil.copytree(args.static, args.out)
    for directory in STANDARD_DIRS:
        os.makedirs(os.path.join(args.out, directory), exist_ok=True)

    if args.manifest:
        placed = set()
        for directory, program in read_manifest(args.manifest):
            destination = os.path.join(args.out, directory, program)
            if destination in placed:
                raise SystemExit(f"{args.manifest}: {directory}/{program} listed twice")
            placed.add(destination)
            source = os.path.join(args.build_dir, f"user_{program}.elf")
            if not os.path.isfile(source):
                raise SystemExit(f"{args.manifest}: no binary {source} for {program}")
            os.makedirs(os.path.dirname(destination), exist_ok=True)
            shutil.copyfile(source, destination)
            os.chmod(destination, 0o755)

    if args.repo:
        shutil.copytree(args.repo, os.path.join(args.out, "var/lib/hpkg/repo"))

    if args.init_rc:
        shutil.copyfile(args.init_rc, os.path.join(args.out, "etc", "init.rc"))

    normalize_modes(args.out)


if __name__ == "__main__":
    sys.exit(main())
