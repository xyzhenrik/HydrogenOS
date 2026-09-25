# HydrogenOS master plan

This is the authoritative product roadmap. `ROADMAP.md` is only a delivery
summary. Product or milestone changes must update this document and, when they
change architecture, add an ADR.

## Product promise

HydrogenOS is a free, open-source Linux desktop for demanding everyday users. It
aims for coherent interaction, predictable behavior, low latency, restrained
resource use, strong defaults, and an original translucent material language.

It is not a macOS clone. Apple code, assets, sounds, naming, layouts, and trade
dress are out of scope. `HydrogenOS` remains a project name until trademark
clearance.

## Preview scope

The first public preview includes an installable image, an original shell,
onboarding, settings, atomic updates and rollback, a curated graphical app path,
and a developer toolbox. Existing Firefox, Dolphin, Konsole, and software-center
components are temporary integrated applications. Original browser, file,
terminal, and store applications are deferred.

## Architecture

```text
Qt Quick shell/settings
        │  versioned org.hydrogen.* D-Bus APIs
        ▼
Rust user services ───── system services (NetworkManager, PipeWire, BlueZ)
        │
        ├──── settings schemas and migrations
        └──── explicit opt-in diagnostics

KWin / Wayland ── Mesa and kernel drivers
Fedora Atomic image ── rpm-ostree deployment and rollback
Flatpak apps ── portals      Toolbox ── developer CLI
```

KWin, systemd, PipeWire, NetworkManager, BlueZ, Mesa, and kernel drivers are
integrated rather than rewritten. v1 does not add private Wayland protocols. A
Smithay compositor is considered only after a documented KWin blocker.

### Stable project interfaces

- User-session services use versioned `org.hydrogen.*` D-Bus names and paths.
- QML design primitives are exported as `Hydrogen.Design 1.0`.
- Persistent settings contain an explicit schema version and tested migrations.
- The Qt/C++ executable hosts contain startup and platform glue, not product
  policy or persistent business logic.

## Experience and performance budgets

Hydrogen Glass has `Full`, `Efficient`, and `Opaque` levels. Hardware capability,
power mode, reduced-transparency preference, and measured frame budget determine
the active level. Reduced motion and reduced transparency are functional modes,
not cosmetic afterthoughts.

- Minimum hardware: p95 frame time at or below 16.67 ms in defined interactions.
- Reference hardware: p95 frame time at or below 8.33 ms.
- Idle after five minutes: under 1% average CPU and initially at most 1.2 GiB
  desktop PSS.
- No continuous decorative animation while idle.

Exceptions require measurements and an accepted ADR.

## Security and privacy baseline

- SELinux enforcing; full-disk encryption offered by the installer.
- Signed public images and atomic updates with a tested rollback path.
- Graphical applications prefer curated Flatpaks and portals.
- Diagnostic transmission is disabled until explicit informed opt-in; the exact
  payload is inspectable before sending.
- CI emits an SBOM and performs dependency, license, and secret checks.

## Milestones and acceptance

### M0 — Foundation

Repository governance, pinned toolchains, CI, initial Atomic image definition,
threat model, and hardware criteria. A clean checkout must produce a bootable VM
image from documented pinned inputs. The scaffold is complete; boot and
reproducibility evidence remain the gate.

### M1 — Design system and glass prototype

Versioned tokens and components, three material levels, windowed/nested shell
prototype, scaling, keyboard access, reduced motion, and reduced transparency.
Visual automation and measured 60/120 Hz baselines are the remaining gate.

### M2 — Shell vertical slice

Production session, wallpaper, panel, dock, launcher, overview, workspaces,
notifications, control center, and complete settings backed by D-Bus contracts.
The session must remain recoverable when a shell component crashes.

### M3 — Product image

Installer, encryption, recovery, signed updates, rollback, curated Flatpaks,
portals, Toolbox, local diagnostics, and separately enabled crash upload. VM
tests must prove install, update, failed update, and rollback.

### M4 — Desktop Preview

Qualify the current Ryzen/RTX dual-GPU development desktop and one controlled
AMD- or Intel-iGPU notebook. Publish checksums, SBOM, known limitations, and a
reproducible release procedure after external design, accessibility, security,
and performance review.

## Release blockers

- A failing install, boot, login, rollback, or recovery path.
- Known data loss, privilege escalation, unsigned release content, or telemetry
  without consent.
- Core shell flows that are not keyboard-operable.
- Frame or idle-resource budgets missed without an accepted exception.
- License provenance that cannot be demonstrated.

