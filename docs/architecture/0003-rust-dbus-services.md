# 0003 — Rust services and versioned D-Bus

- Status: accepted
- Date: 2026-09-25

## Context

The visual layer needs stable, testable access to system state without embedding
product policy in QML or tying services to a specific UI process.

## Decision

Implement Hydrogen-owned state and policy in memory-safe Rust services. Expose
versioned session APIs under `org.hydrogen.*` through D-Bus. Store schema versions
with persistent settings. Do not add a private Wayland protocol in v1.

## Alternatives considered

- Direct QML access to every system API: fast initially but difficult to test,
  supervise, migrate, or secure.
- In-process Rust/Qt bridge for all logic: lower call overhead but tighter crash
  and lifecycle coupling.

## Consequences

D-Bus contracts become public project interfaces and require contract tests and
compatibility review. High-frequency rendering state stays local to the UI.

