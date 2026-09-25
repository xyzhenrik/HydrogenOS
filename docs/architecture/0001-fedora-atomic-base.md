# 0001 — Fedora Atomic product base

- Status: accepted
- Date: 2026-09-25

## Context

HydrogenOS needs modern graphics, atomic updates, rollback, SELinux, and a clear
separation between the operating system and user applications. Development takes
place on an Arch-family host, but the shipped product needs controlled releases.

## Decision

Build the product image from Fedora Atomic/Kinoite inputs. Keep host development
portable through pinned toolchains and containers. Use rpm-ostree deployment and
rollback for v1 and Flatpak/Toolbox above the base image.

## Alternatives considered

- Arch product base: excellent iteration speed but transfers snapshot, release,
  rollback, and repository operations to the project immediately.
- NixOS: strong reproducibility but a larger productization and contributor
  learning cost for the first preview.

## Consequences

The project inherits Fedora release cadence and SELinux integration. Product
packages cannot be installed ad hoc; they must be part of a reviewed image.

