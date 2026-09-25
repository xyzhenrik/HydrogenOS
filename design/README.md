# Hydrogen design system

`Hydrogen.Design 1.0` is the stable QML import for shared tokens and primitives.
It currently contains the M1 foundations, not a complete application toolkit.

Material levels:

- `Full`: translucent material plus GPU highlight; compositor blur is added by
  the KWin effect in a later milestone.
- `Efficient`: simpler translucent fill without the custom shader.
- `Opaque`: accessible and power-conscious solid surface.

Components must remain useful with reduced motion, reduced transparency,
keyboard-only input, and 100–200% scale. Do not encode product policy here.

