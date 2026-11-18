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
- **Temporal warheads from any weapon slot** - original fork implementation
  `c2341b52` (2026-09-22), ported to VentureLW 2026-11-18.  The platform version
  does not reuse the fork's injection points: `0x71AB10` belongs to a co-loaded
  official Phobos, and Ares replaces the middle of `TemporalClass::Update`, so the
  platform creates the missing Temporal instance at the detonation site and
  rescales the warp step on the update tail that both hosts share.
- **ROF curve (weapon-level spin-up)** - original fork implementation
  `47023891` / `b950ac50`, per-weapon chains `6c54bd6f` (2026-10), ported to
  VentureLW 2026-11-18.  The platform version keeps the same seven `ROFCurve.*`
  keys and the same curve, and hooks the two `Fire_At` reload sites and the two
  weapon-type INI epilogues; it reads the weapon from the `Fire_At` argument on
  the `DiskLaser=yes` branch, where the fork's EBX-based read silently missed.
- **Armed engineers** - original fork implementation `c2341b52` (2026-09-26),
  ported to VentureLW 2026-11-18.  The platform version keeps the same two
  layers (attack-clamp hook + `InfantryClass::MouseOverObject` wrapper) but takes
  the vtable slot over at run time with a chain to the previous implementation
  instead of the fork's static VTABLE patch.
- **Tactical view zoom** - original fork implementation `51fa3628`, follow-up
  fixes `8c43bd3f` / `ff77ea04` / `65043b38` / `a88c5d48` / `b62f46ce` /
  `70c86561` (2026-10), ported to VentureLW 2026-11-18.  The platform keeps the
  same 15 `ViewZoom.*` keys, level table and algorithm (the keys move from the
  fork's `[Phobos]` section to the platform's `[VentureLW]` section) and fixes
  the fork's bug that dropped the whole-map level when `full` was written
  explicitly.  The three commands are registered through the platform command
  registry (`Venture View Zoom In` / `Venture View Zoom Out` /
  `Venture Reset View Zoom`).

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
