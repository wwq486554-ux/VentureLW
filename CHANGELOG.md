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

- `COPYING` (GPL-3.0), `CREDITS.md` and this changelog.

### Base machinery

- Everything listed in the base README is present unchanged: startup pipeline,
  logging, INI key registry, extension storage with savegame persistence, event
  channel, command registration, lineage mutual exclusion, host detection set,
  YRpp binding gate, and the verification tooling (16 engine-free tests, static
  checks, Wine probes).  The base's two Example modules are kept as smoke tests.
