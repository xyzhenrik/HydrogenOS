# Deterministic shell visual tests

The visual suite captures the production shell prototype at 100%, 150%, and
200% scale and compares every pixel with reviewed baselines. On failure, CTest
leaves the actual image and a high-contrast red difference image in
`build/test-artifacts/visual/`; CI uploads that directory as an artifact.

Determinism comes from a fixed 1280×760 logical window, the software Qt Quick
backend, a fixed `09:41` clock, reduced motion, the DejaVu Sans font, `C.UTF-8`,
and a pinned Qt environment in CI. The capture path is active only when
`hydrogen-shell --visual-test PATH` is used.

DejaVu Sans is a test-environment prerequisite, not a HydrogenOS runtime
dependency. Capture fails instead of silently substituting a host font. Fedora
CI installs `dejavu-sans-fonts`; Arch-family development hosts can provide it
through `ttf-dejavu`.

## Run comparisons

```sh
make build
ctest --test-dir build --output-on-failure -R visual-shell
```

## Intentionally update baselines

Only update baselines after reviewing the UI change at every scale:

```sh
make build
./tests/visual/update-baselines.sh build
ctest --test-dir build --output-on-failure -R visual-shell
git diff -- tests/visual/baselines
```

Commit baseline changes in the same pull request as the reviewed visual change.
Never update them merely to silence an unexplained failure. Baselines generated
with another Qt version must be reviewed and called out in the pull request.
