# Credits

## VentureLW platform

- **laowang** - platform bootstrap (identity, build, verification), feature
  modules, documentation.  Ported `RadialIndicator` into the platform.

## Base

- **The VentureCore Contributors** - the MIT-licensed starter base
  (v2.1.0) this platform is built from: startup, logging, INI layer, extension
  storage and savegame trailer, event channel, command registration, lineage
  exclusion, host detection, build and verification tooling.

## Ported gameplay features (from Phobos-LWModified)

- **laowang** - original implementation of the Phobos-LWModified gameplay
  features and of the fork's additions.
- **Radial indicator large-range drawing** - original fork implementation
  `41bc10fe` (docs `1865743c`, 2026-10-24), ported to VentureLW 2026-11-14.

## Pending attribution (not in this build yet)

- **Upstream trajectory system (PR #2060, "New trajectory system")** - original
  work by **CrimRecya** (PR #1582), reopened by **TaranDahl** (PR #2060).  It is
  integrated in Phobos-LWModified and will be ported in a later batch; the
  attribution rule above (`CrimRecya` original / `TaranDahl` reopen) applies.
- **Ares / Phobos / Kratos** - behavioural reference only; no code is copied.
  Their own credits and licenses apply to them.

## Third-party

- **YRpp** (engine wrapper headers) and **SyringeEx** (injector) - external
  build/runtime dependencies, not redistributed; see `NOTICE.md`.
