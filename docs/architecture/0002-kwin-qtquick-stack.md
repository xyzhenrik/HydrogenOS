# 0002 — KWin and Qt Quick desktop stack

- Status: accepted
- Date: 2026-09-25

## Context

Hardware support, Wayland correctness, XWayland, input, multi-monitor behavior,
and accessibility would dominate a new compositor effort before the product
experience can be validated.

## Decision

Use KWin as the v1 compositor. Build original shell and settings surfaces with Qt
Quick/QML and GPU shaders. Keep Qt/C++ hosts thin. Consider Smithay only when a
measured, documented product requirement cannot be implemented with KWin.

## Alternatives considered

- New Smithay compositor immediately: maximum control with unacceptable v1 scope
  and hardware risk.
- COSMIC fork: Rust-first but couples the product to another desktop's evolving
  architecture and experience.

## Consequences

HydrogenOS depends on KDE/Qt interfaces and compatible licensing. Glass effects
must degrade gracefully and remain inside explicit frame and power budgets.

