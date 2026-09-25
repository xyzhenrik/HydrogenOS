# Security policy

## Supported versions

HydrogenOS has not published a supported release. Security fixes currently
target the `main` branch and will be documented in release notes once previews
exist.

## Reporting a vulnerability

Do not open a public issue for a suspected vulnerability. Use GitHub's private
security advisory feature for `xyzhenrik/HydrogenOS`. Include affected revision,
impact, reproduction steps, and any proposed mitigation.

Maintainers should acknowledge a complete report within seven days. No bounty or
embargo duration is promised during the pre-release phase.

## Project guarantees

- No diagnostic data is transmitted without explicit, informed opt-in.
- Secrets and private keys are never stored in the repository or image layers.
- Release images and updates must be signed before public distribution.
- SELinux remains enforcing in product images.

See `docs/THREAT_MODEL.md` for current trust boundaries and known gaps.

