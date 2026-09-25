# Initial threat model

Status: draft for M0. Update this document whenever a change adds privilege,
network transmission, persistent sensitive data, update trust, or authentication.

## Protected assets

- User files, credentials, session input, clipboard, notifications, and settings
- Update/signing keys and build provenance
- Boot integrity and the ability to recover or roll back
- Consent state and diagnostic payloads

## Trust boundaries

- Untrusted Flatpak application ↔ portal and compositor
- User-session Hydrogen service ↔ privileged system service
- Update client ↔ remote image registry/repository
- Shell/QML content ↔ D-Bus services
- Build worker ↔ release-signing process

## Initial controls

- Wayland isolation and portals for graphical applications
- SELinux enforcing in the product image
- Minimal user-session D-Bus services with explicit method contracts
- Atomic, signed deployments with a retained rollback deployment
- No network diagnostics before explicit consent
- CI dependency review, secret scanning, SBOM, and artifact attestations

## Known pre-release gaps

- Release signing keys and offline signing procedure are not established.
- Installer encryption and recovery flows are not implemented.
- D-Bus authorization policy is provisional until privileged services exist.
- The settings daemon currently has normal user-session access to the home
  directory; narrower filesystem mediation is required before M3.
- The developer image has not completed adversarial or supply-chain review.

## Review checklist

For each affected boundary, document attacker capability, data handled, least
privilege, authentication/authorization, failure behavior, logging, retention,
user consent, rollback, and tests. Security controls must fail closed where doing
so cannot cause data loss.
