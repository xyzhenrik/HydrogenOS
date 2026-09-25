# HydrogenOS

HydrogenOS is a free and open-source Linux desktop project focused on a calm,
responsive, resource-conscious experience with an original translucent material
system. It is not a macOS clone and does not use Apple code, assets, sounds, or
trade dress.

The project is in **foundation / developer preview** status. Nothing here should
be used as a daily-driver operating system yet.

## Architecture at a glance

- Fedora Atomic/Kinoite-derived product image
- KWin as the Wayland compositor for v1
- Qt Quick/QML shell and settings surfaces
- Rust services with versioned `org.hydrogen.*` D-Bus APIs
- Flatpak-first graphical applications and Toolbox for development tools

See [the master plan](docs/MASTER_PLAN.md) and
[architecture decisions](docs/architecture/README.md) before making structural
changes.

## Build the desktop prototypes

Requirements: CMake 3.25+, Ninja, Qt 6.8+ with Quick Controls 2.

```sh
make build
make run-shell
make run-settings
```

The shell binary currently launches a safe windowed prototype. It does not
replace the running desktop shell.

## Build and test the Rust service

Rust is pinned in `rust-toolchain.toml`.

```sh
cargo test --workspace --all-targets
cargo run -p hydrogen-settingsd
```

## Status

M0 is scaffolded and M1 has an executable visual prototype. Installer, signed
updates, a production session, and hardware qualification remain future
milestones. See [ROADMAP.md](ROADMAP.md) for the exact gates.

## License

HydrogenOS original code is licensed under GPL-3.0-or-later. Contributions must
be compatible with that license. Third-party components retain their own
licenses.

