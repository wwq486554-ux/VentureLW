# Changelog

All notable changes to VentureLW are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses semantic versioning.  VentureLW keeps its own version line,
independent of the base it was copied from (see `README.md`).  The history of the
MIT-licensed VentureCore base is kept in that repository's `CHANGELOG.md`.

## [0.1.0] - 2026-11-14

First build of the VentureLW platform, bootstrapped from the MIT-licensed
VentureCore v2.1.0 starter base (method: copy the source tree and re-apply the
platform identity; see the base `docs/Renaming.md`).

### Changed (platform identity)

- Platform name is **`VentureLW`**: the DLL, INI, log and version resource are
  `VentureLW.dll` / `VentureLW.ini` / `VentureLW.log` / `VentureLW v0.1.0.0`
  (derived from `VC_PLATFORM_NAME` and `ProjectName`, as documented in the base
  `docs/Renaming.md`).
- New savegame trailer id `VC_SAVEGAME_ID = 0x564C5731` ('V','L','W','1');
  savegames written by the VentureCore base or by Phobos-LWModified are **not**
  compatible with this platform's extension state (expected - the platform is a
  new lineage branch; see `docs/SaveFormat.md`).
- Version line starts at **0.1.0** (the base lineage version is unrelated).
- Combined work licensed **GPL-3.0** (`COPYING`); the MIT base notice is retained
  (`LICENSE.md`, `NOTICE.md`).

### Added

- **Radial indicator large-range drawing** (`[Features] RadialIndicator`,
  default on) - ported from Phobos-LWModified commits `41bc10fe` +
  `1865743c`.  The engine's ellipse rasterizer uses 32-bit integer
  discriminants, so the radial indicator's ring disappears completely once the
  radius grows large (roughly a 30 cell range and beyond).  The platform now
  watches both `call [vtable+0x20]` sites of `Game::DrawRadialIndicator`
  (`0x456B57`, size 8 - outer ring; `0x456BEB`, size 7 - inner ring) and draws
  the outline itself with double-precision scan lines once the conservative
  limit is crossed; below it the engine path is replayed unchanged.  Rendering
  only - no game state is written.  **No new INI keys; no savegame change.**
  See `docs/Fixed-or-Improved-Logics.md` and `docs/HookCatalogue.md`.
- `COPYING` (GPL-3.0), `CREDITS.md` and this changelog.
- A base-sync helper for pulling later VentureCore fixes into the platform:
  `scripts/base-sync/` (semi-automatic; the identity of this platform is re-applied
  by hand after each sync).

### Base machinery

- Everything listed in the base README is present unchanged: startup pipeline,
  logging, INI key registry, extension storage with savegame persistence, event
  channel, command registration, lineage mutual exclusion, host detection set,
  YRpp binding gate, and the verification tooling (16 engine-free tests, static
  checks, Wine probes).  The base's two Example modules are kept as smoke tests.
