#!/usr/bin/env python3
"""Build .hpkg packages and local package repositories for hpkg.

  hpkg_repo.py pack <source-dir> <output.hpkg>
  hpkg_repo.py index <repository-dir>
  hpkg_repo.py build <packages-dir> <repository-dir>

A source directory holds a `manifest` file ("key value" lines: name, version,
description, depends) and the files to install below `root/`. `index` reads
every *.hpkg in a repository directory, validates it like the on-target
hpkg does, and writes the `index` file. `build` packs each subdirectory of
packages-dir into the repository and indexes it. Format: docs/HPKG.md.
"""
import argparse
import hashlib
import os
import re
import shutil
import struct
import sys

MAGIC = b"HPK1"
INDEX_HEADER = "HPKG-INDEX 1"
NAME_RE = re.compile(r"^[a-z0-9][a-z0-9+_-]{0,31}\Z")
VERSION_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9.+~_-]{0,31}\Z")
FILE_NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9.+_-]*\.hpkg\Z")
PATH_MAX = 200
FILES_MAX = 4096
MANIFEST_MAX = 4096
DESCRIPTION_MAX = 120
DEPENDS_MAX = 16
RESERVED_SUFFIXES = (".hpkg-new", ".hpkg-old")
DATABASE_ROOT = "/var/lib/hpkg"


class PackageError(Exception):
    pass


def path_key(path):
    """Sort key matching hpkg_path_compare: '/' sorts below every other byte."""
    return path.encode("ascii").replace(b"/", b"\x01")


def check_path(path):
    if not 2 <= len(path) <= PATH_MAX or not path.startswith("/") or path.endswith("/"):
        raise PackageError(f"unsafe path {path!r}")
    if any(not 0x21 <= ord(c) <= 0x7E for c in path):
        raise PackageError(f"unsafe path {path!r}")
    if any(part in ("", ".", "..") for part in path[1:].split("/")):
        raise PackageError(f"unsafe path {path!r}")
    if path.endswith(RESERVED_SUFFIXES):
        raise PackageError(f"reserved file name in {path!r}")
    if path == DATABASE_ROOT or path.startswith(DATABASE_ROOT + "/"):
        raise PackageError(f"{path!r} is inside the package database")


def parse_manifest(text):
    """Returns the manifest fields as a dict; depends becomes a list."""
    meta = {"description": "", "depends": []}
    for line in text.split("\n"):
        if not line:
            continue
        key, separator, value = line.partition(" ")
        if not separator:
            raise PackageError(f"malformed manifest line {line!r}")
        if key == "name":
            if not NAME_RE.match(value):
                raise PackageError(f"invalid package name {value!r}")
            meta["name"] = value
        elif key == "version":
            if not VERSION_RE.match(value):
                raise PackageError(f"invalid version {value!r}")
            meta["version"] = value
        elif key == "description":
            if len(value) > DESCRIPTION_MAX or any(not 0x20 <= ord(c) <= 0x7E for c in value):
                raise PackageError("invalid description")
            meta["description"] = value
        elif key == "depends":
            names = value.split()
            if len(names) > DEPENDS_MAX or len(set(names)) != len(names):
                raise PackageError("invalid dependency list")
            for dependency in names:
                if not NAME_RE.match(dependency):
                    raise PackageError(f"invalid dependency name {dependency!r}")
            meta["depends"] = names
    if "name" not in meta or "version" not in meta:
        raise PackageError("manifest lacks name or version")
    if meta["name"] in meta["depends"]:
        raise PackageError("package depends on itself")
    return meta


def build_archive(manifest_text, files):
    """files: list of (path, mode, data). Returns the archive bytes."""
    if len(files) > FILES_MAX:
        raise PackageError("too many files")
    manifest = manifest_text.encode("ascii")
    if len(manifest) > MANIFEST_MAX:
        raise PackageError("manifest too large")
    parse_manifest(manifest_text)
    out = bytearray(MAGIC)
    out += struct.pack("<I", len(manifest)) + manifest
    out += struct.pack("<I", len(files))
    previous = None
    for path, mode, data in sorted(files, key=lambda item: path_key(item[0])):
        check_path(path)
        if previous is not None and path.startswith(previous + "/"):
            raise PackageError(f"{previous!r} is both a file and a directory")
        previous = path
        encoded = path.encode("ascii")
        out += struct.pack("<I", len(encoded)) + encoded
        out += struct.pack("<II", mode, len(data))
        out += hashlib.sha256(data).digest() + data
    out += hashlib.sha256(bytes(out)).digest()
    return bytes(out)


def read_archive(data, label):
    """Validates an archive; returns (meta, file_count)."""
    def take(count):
        nonlocal position
        if position + count > end:
            raise PackageError(f"{label}: truncated")
        chunk = data[position:position + count]
        position += count
        return chunk

    if len(data) < 4 + 32 or not data.startswith(MAGIC):
        raise PackageError(f"{label}: not a hpkg package")
    end = len(data) - 32
    if hashlib.sha256(data[:end]).digest() != data[end:]:
        raise PackageError(f"{label}: archive checksum mismatch")
    position = 4
    (manifest_length,) = struct.unpack("<I", take(4))
    if manifest_length > MANIFEST_MAX:
        raise PackageError(f"{label}: manifest too large")
    meta = parse_manifest(take(manifest_length).decode("ascii"))
    (count,) = struct.unpack("<I", take(4))
    if count > FILES_MAX:
        raise PackageError(f"{label}: too many files")
    previous = None
    for _ in range(count):
        (path_length,) = struct.unpack("<I", take(4))
        path = take(path_length).decode("ascii")
        mode, size = struct.unpack("<II", take(8))
        digest = take(32)
        content = take(size)
        check_path(path)
        if mode & ~0o777:
            raise PackageError(f"{label}: {path}: unsupported mode bits")
        if hashlib.sha256(content).digest() != digest:
            raise PackageError(f"{label}: checksum mismatch for {path}")
        if previous is not None:
            if path_key(previous) >= path_key(path):
                raise PackageError(f"{label}: file table not sorted or duplicate")
            if path.startswith(previous + "/"):
                raise PackageError(f"{label}: {previous} is both a file and a directory")
        previous = path
    if position != end:
        raise PackageError(f"{label}: trailing data")
    return meta, count


def pack(source, output):
    with open(os.path.join(source, "manifest"), encoding="ascii") as handle:
        manifest_text = handle.read()
    root = os.path.join(source, "root")
    files = []
    for current, _directories, names in os.walk(root):
        for name in names:
            full = os.path.join(current, name)
            path = "/" + os.path.relpath(full, root).replace(os.sep, "/")
            executable = os.stat(full).st_mode & 0o100
            with open(full, "rb") as handle:
                files.append((path, 0o755 if executable else 0o644, handle.read()))
    archive = build_archive(manifest_text, files)
    with open(output, "wb") as handle:
        handle.write(archive)
    return parse_manifest(manifest_text)


def build_index(repository):
    lines = [INDEX_HEADER]
    seen = set()
    for file_name in sorted(os.listdir(repository)):
        if not file_name.endswith(".hpkg"):
            continue
        if not FILE_NAME_RE.match(file_name) or len(file_name) > 64:
            raise PackageError(f"{file_name}: unusable repository file name")
        with open(os.path.join(repository, file_name), "rb") as handle:
            data = handle.read()
        meta, _count = read_archive(data, file_name)
        if meta["name"] in seen:
            raise PackageError(f"{file_name}: second package named {meta['name']}")
        seen.add(meta["name"])
        depends = ",".join(meta["depends"]) or "-"
        line = " ".join([meta["name"], meta["version"], str(len(data)),
                         hashlib.sha256(data).hexdigest(), depends, file_name])
        if meta["description"]:
            line += " " + meta["description"]
        lines.append(line)
    with open(os.path.join(repository, "index"), "w", encoding="ascii") as handle:
        handle.write("\n".join(lines) + "\n")
    return len(lines) - 1


def build_repository(packages, repository):
    shutil.rmtree(repository, ignore_errors=True)
    os.makedirs(repository)
    for name in sorted(os.listdir(packages)):
        source = os.path.join(packages, name)
        if not os.path.isdir(source):
            continue
        with open(os.path.join(source, "manifest"), encoding="ascii") as handle:
            meta = parse_manifest(handle.read())
        pack(source, os.path.join(repository, f"{meta['name']}-{meta['version']}.hpkg"))
    return build_index(repository)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    pack_parser = commands.add_parser("pack", help="pack one source directory")
    pack_parser.add_argument("source")
    pack_parser.add_argument("output")
    index_parser = commands.add_parser("index", help="write the index of a repository")
    index_parser.add_argument("repository")
    build_parser = commands.add_parser("build", help="pack all packages and index them")
    build_parser.add_argument("packages")
    build_parser.add_argument("repository")
    args = parser.parse_args()

    try:
        if args.command == "pack":
            pack(args.source, args.output)
        elif args.command == "index":
            build_index(args.repository)
        else:
            build_repository(args.packages, args.repository)
    except (PackageError, OSError) as error:
        sys.exit(f"hpkg_repo: {error}")


if __name__ == "__main__":
    main()
