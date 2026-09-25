# HydrogenOS agent instructions

## Mission

Build an original, free, fast, accessible Linux desktop. Do not copy Apple code,
assets, sounds, layouts, names, or trade dress. The v1 stack is Fedora Atomic,
KWin, Qt Quick/QML, and Rust services; do not replace these foundations without
an accepted ADR.

## Working boundaries

- Keep changes scoped to the assigned issue. Do not silently invent product,
  protocol, licensing, security, or telemetry policy.
- Never work directly on `main`; use a branch or worktree. Do not use destructive
  Git commands or discard unrelated user changes.
- Public IPC belongs under a versioned `org.hydrogen.*` D-Bus interface. v1 must
  not add private Wayland protocols.
- Product logic belongs in Rust services. QML owns presentation and interaction;
  the C++ Qt hosts stay thin.
- New runtime dependencies require a documented license, maintenance, security,
  and resource-cost review in the PR.
- Network transmission, telemetry, privileged services, and new persistent data
  require a threat-model update and human approval.
- Architecture or public-interface changes require an ADR. Performance-budget
  exceptions require measured evidence and an ADR.

## Context to read

- Use `docs/MASTER_PLAN.md` for product scope and milestone acceptance.
- Use `docs/architecture/` when changing boundaries, APIs, or dependencies.
- Use `docs/THREAT_MODEL.md` for privileged, persistent, update, or network work.
- Use `image/README.md` when touching the operating-system image.

## Verification

Run the smallest relevant checks and fix failures caused by the change:

```sh
make build
make test
make lint
```

CI is authoritative for Rust when Cargo is unavailable locally. Never delete or
weaken a failing test to make CI green. Document hardware-only checks that could
not be run.

## Definition of done

- Behavior and error paths are tested.
- Keyboard use, focus, scaling, reduced motion, and reduced transparency remain
  usable for UI work.
- Relevant documentation, translations, contracts, and performance evidence are
  updated.
- Code, commits, issues, and normative documentation are in English.
- No secrets, generated build products, proprietary assets, or user data enter
  the repository.

