# Roadmap

The authoritative product direction is in `docs/MASTER_PLAN.md`. This file is a
compact delivery view and must not introduce new product requirements.

## M0 — Foundation

- [x] Repository, governance, architecture decisions, and agent guidance
- [x] Pinned Rust toolchain and Qt minimum version
- [x] CI for C++, QML, Rust, policy, and SBOM artifact generation
- [x] Initial Fedora Atomic developer-image definition
- [ ] Boot and smoke-test the developer image in a VM
- [ ] Record the first reproducibility result and image digest

## M1 — Design system and glass prototype

- [x] Versioned `Hydrogen.Design 1.0` QML module
- [x] Full, Efficient, and Opaque material levels
- [x] Windowed shell and settings prototypes
- [x] Reduced-motion and reduced-transparency controls in the prototype
- [x] Add Qt Quick Test visual/keyboard coverage
- [x] Add reproducible frame-time and idle-resource measurement tooling
- [ ] Capture 60 Hz and 120 Hz frame-time baselines on reference hardware

## M2 — Shell vertical slice

- [ ] Layer-shell integration and supervised production session
- [ ] Launcher, overview, notifications, control center, and workspaces
- [ ] Complete settings categories backed by versioned D-Bus contracts

## M3 — Product image

- [ ] Installer, encryption, recovery, signed updates, and rollback
- [ ] Curated Flatpak experience and Toolbox integration
- [ ] Explicit opt-in diagnostics workflow

## M4 — Desktop Preview

- [ ] Reference hardware qualification and public release artifacts
- [ ] External design, accessibility, performance, and security reviews
