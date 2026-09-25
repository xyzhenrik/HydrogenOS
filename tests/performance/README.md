# Performance measurements

HydrogenOS keeps CI performance smoke checks separate from hardware
qualification. CI proves that the workload runs, the result schema is valid,
and reports are retained. Its offscreen software renderer cannot prove a 60 Hz
or 120 Hz product budget.

## Frame-time scenario

Build a release binary before collecting evidence:

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
```

Run each target on a matching physical display in a Wayland session. Use the
stable system name from `docs/HARDWARE_MATRIX.md` and keep the window visible
and unobstructed:

```sh
QT_SCALE_FACTOR=1 build-release/shell/hydrogen-shell \
  --benchmark-output /tmp/hydrogen-frame-60.json \
  --benchmark-refresh 60 \
  --benchmark-warmup 120 \
  --benchmark-frames 600 \
  --hardware-label development-ryzen-9800x3d-radeon

QT_SCALE_FACTOR=1 build-release/shell/hydrogen-shell \
  --benchmark-output /tmp/hydrogen-frame-120.json \
  --benchmark-refresh 120 \
  --benchmark-warmup 240 \
  --benchmark-frames 1200 \
  --hardware-label reference-notebook
```

The deterministic `prototype_card_and_dock_transition` workload fixes the
window size, clock, font, and animation. Startup and shader compilation are
excluded by the warm-up. Measurements use presentation intervals from Qt
Quick's `frameSwapped` signal. Percentiles use the nearest-rank method.
`frames_over_budget` counts raw intervals above the target budget;
`dropped_frames` estimates missed presentation slots by rounding each interval
to the nearest target interval.

A report is qualification-eligible only when it comes from an optimized release
build, has a hardware label, runs on Wayland with hardware acceleration, and the
detected display rate matches the 60 Hz or 120 Hz target. Eligibility does not
mean that the budget passed. For M1, `metrics.p95_ms` must be at most 16.67 ms
at 60 Hz and at most 8.33 ms at 120 Hz. Attach the JSON to the review that
records a baseline, then copy an accepted result into
`tests/performance/baselines/<hardware-label>/` in that review. Do not hand-edit
measurements. The provisional `reference-notebook` label is not accepted until
the exact notebook SKU is recorded in the hardware matrix.

## Idle resources

Wait until the session has reached its normal idle state, collect every
Hydrogen-owned session process ID, and measure for five minutes:

```sh
tests/performance/measure-idle.sh \
  --pid 1234,1235 \
  --duration 300 \
  --output /tmp/hydrogen-idle.json \
  --hardware-label development-ryzen-9800x3d-radeon
```

The report contains average CPU use across the explicit PID set and its final
summed proportional set size (PSS). Use the default `shell-prototype` scope for
today's single prototype process. A report is eligible only with a duration of
at least 300 seconds, a hardware label, and an explicit
`--scope hydrogen-session`; that scope requires every Hydrogen-owned session
process in `--pid`. The current helper deliberately does not claim whole-desktop
resource use: KWin and integrated third-party services must be measured
separately when the production session exists.

The helper is Linux-specific and uses procfs plus the base-system `bash`,
`awk`, `sed`, and `getconf` tools. It adds no Hydrogen runtime dependency.

## CI and local smoke checks

Run the deterministic, non-qualifying checks with:

```sh
make performance-smoke
```

CTest writes reports below `build/test-artifacts/performance/`. GitHub Actions
retains that directory even when another Qt test fails. Software-rendered CI
numbers may detect gross regressions but must never be copied into hardware
baselines or used to close the M1 performance gate.
