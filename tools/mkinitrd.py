#!/usr/bin/env python3
import os
import sys


def pad4(f, count):
    pad = (4 - (count % 4)) % 4
    if pad:
        f.write(b"\x00" * pad)


def write_entry(f, name, data, mode):
    name = name.lstrip("/")
    if not name:
        raise ValueError("empty name in initrd entry")
    namesize = len(name) + 1
    filesize = len(data)
    header = (
        "070701"
        + f"{0:08x}"  # ino
        + f"{mode:08x}"
        + f"{0:08x}"  # uid
        + f"{0:08x}"  # gid
        + f"{1:08x}"  # nlink
        + f"{0:08x}"  # mtime
        + f"{filesize:08x}"
        + f"{0:08x}"  # devmajor
        + f"{0:08x}"  # devminor
        + f"{0:08x}"  # rdevmajor
        + f"{0:08x}"  # rdevminor
        + f"{namesize:08x}"
        + f"{0:08x}"  # check
    )
    f.write(header.encode("ascii"))
    f.write(name.encode("ascii") + b"\x00")
    pad4(f, CPIO_HDR_LEN + namesize)
    f.write(data)
    pad4(f, filesize)


def usage():
    print("usage: mkinitrd.py <output> <root-directory>")
    sys.exit(1)


CPIO_HDR_LEN = 110
CPIO_MODE_DIR = 0o040755
CPIO_MODE_FILE = 0o100755


def collect(root):
    """Returns (name, mode, data) for every entry below root, each directory
    ahead of its contents."""
    entries = []
    for current, dirs, files in os.walk(root):
        dirs.sort()
        relative = os.path.relpath(current, root)
        if relative != ".":
            entries.append((relative, CPIO_MODE_DIR, b""))
        for name in sorted(files):
            path = os.path.join(current, name)
            with open(path, "rb") as src:
                data = src.read()
            entries.append((os.path.normpath(os.path.join(relative, name)),
                            CPIO_MODE_FILE, data))
    return entries


def main():
    if len(sys.argv) != 3:
        usage()
    out_path, root = sys.argv[1], sys.argv[2]
    if not os.path.isdir(root):
        raise SystemExit(f"not a directory: {root}")

    with open(out_path, "wb") as f:
        for name, mode, data in collect(root):
            write_entry(f, name, data, mode)
        write_entry(f, "TRAILER!!!", b"", CPIO_MODE_FILE)


if __name__ == "__main__":
    main()
