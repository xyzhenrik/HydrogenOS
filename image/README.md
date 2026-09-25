# HydrogenOS developer image

This is an **unreleased developer image**, not an installer or daily-driver OS.
It derives from Fedora Kinoite 44, keeps Plasma as an explicit fallback, installs
the Hydrogen prototypes, and starts the settings service in user sessions.

The base manifest is pinned by digest in `Containerfile`. When it changes, record
the old/new digest, upstream version, build result, boot result, and rollback
result in the pull request.

## Build

Requirements: Podman 5+ with at least 20 GiB free disk space.

```sh
./image/build.sh
```

The result is a local OCI image named `localhost/hydrogen-os:dev`. Generating an
installer is intentionally not automated until the image passes its first boot,
session, update, and rollback tests.

## Current limitations

- The developer session is Plasma with Hydrogen prototypes available; it is not
  the M2 production shell session.
- Secure Boot and release signing are not configured.
- No public registry or update stream exists.
- The exact image has not yet passed VM or physical-hardware qualification.

