# Contributing to HydrogenOS

HydrogenOS welcomes contributions that advance an original, accessible, and
resource-conscious Linux desktop.

## Before coding

1. Read `AGENTS.md` and the relevant milestone in `docs/MASTER_PLAN.md`.
2. Open or claim a narrowly scoped issue using the agent-task template.
3. For architecture, public API, security policy, telemetry, or budget changes,
   propose an ADR and obtain maintainer agreement before implementation.

## Development flow

- Branch from `main` using `type/short-description`.
- Keep one user-visible objective per pull request.
- Add tests with the behavior, not in a later cleanup.
- Describe untested hardware paths explicitly.
- Use English for code, commits, issues, and normative documentation.

Run before opening a pull request:

```sh
make check
```

Rust-only changes should also pass:

```sh
cargo test --workspace --all-targets
cargo clippy --workspace --all-targets -- -D warnings
```

## Commit style

Use a concise imperative subject, optionally prefixed by the subsystem, for
example `shell: respect reduced motion in the dock`.

## Licensing

By contributing, you agree that your contribution is licensed under
GPL-3.0-or-later. Do not submit code or assets that cannot be redistributed
under compatible terms.

