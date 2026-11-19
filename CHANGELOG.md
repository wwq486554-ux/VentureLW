# Changelog

All notable changes to VentureLW are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses semantic versioning.  VentureLW keeps its own version line,
independent of the base it was copied from (see `README.md`).  The history of the
MIT-licensed VentureCore base is kept in that repository's `CHANGELOG.md`.

## [Unreleased]

### Added

- **SuddenDeath warheads** (`[Features] SuddenDeath`, default on; the behaviour
  itself is opt-in per warhead) - ported from Phobos-LWModified's `SuddenDeath`
  feature.  A warhead that writes `SuddenDeath=yes` kills whatever its detonation
  reaches, past damage, armour, `Verses`, `Immune=yes`, Iron Curtain and Force
  Shield; the hit is recorded inside the damage call and executed at the head of
  the next logic frame, so the kill never re-enters the object bookkeeping the
  detonation is walking.  `SuddenDeath.Mode=erase` removes the object instead of
  killing it (no wreck, no `DeathWeapon`, no revenge weapon, no death animation,
  no kill credit).  The same detonation sweeps the `CellSpread + 0.5` area: ore
  is cleared completely, bridges collapse through the engine's IonCannon branch
  and terrain objects (trees, tiberium pillars) are removed without a health
  check.  A unit carrying such a weapon gets the Attack cursor on `Immune=yes`
  targets and may fire at a 0% `Verses` target.  Eight new hooks
  (`0x701900` / `0x75DEA0` / `0x55B4E1` / `0x6FCB64` / `0x417F63` / `0x51F179` /
  `0x74049A` / `0x489286`), catalogue 54 -> 62.  **No platform INI keys and no
  savegame change** - unlike the fork, the keys live in a type-level table and
  are never serialized.  See `docs/New-or-Enhanced-Logics.md` (English + 中英对照)
  and the platform chapter of `guides/即死弹头说明.txt`.
- New platform API `venture::RegisterScenarioClearObserver()` (declared in
  `src/Core/Platform.h`, engine-free registry in `src/Core/ScenarioClear.h`):
  a feature registers one `__cdecl` callback that runs after the platform reset
  its extension state on a scenario clear (new match, retry, main menu, savegame
  load).  `SuddenDeath` is its first user; `tests/test_scenario_clear.cpp`
  covers the registry contract (de-duplication, null rejection, order and
  snapshot semantics).

- **Temporal warheads from any weapon slot** (`[Features] TemporalAnySlot`,
  default on) - ported from Phobos-LWModified commit `c2341b52`.  The engine only
  creates a techno's Temporal instance for a slot-0 `Temporal=yes` warhead and
  then dereferences it without a null check, so a Temporal warhead from any other
  slot (or from `AirburstWeapon` / `ShrapnelWeapon` / `ForceWeapon` / Ares
  superweapons) crashed the game at `0x71AF4D`.  The platform creates the missing
  instance on the first Temporal detonation (`0x4694BB`, size 6) and, on the shared
  tail of `TemporalClass::Update` (`0x71AB08`, size 7, reached by vanilla and by
  Ares + official Phobos alike), rescales the per-frame erase step to the weapon
  that actually fired the warhead instead of the techno's current selection.
  **No new INI keys (no tuning needed); no savegame change** - the recorded
  weapon is transient per-techno state.  (Unlike the fork, the platform has no
  `[General] TemporalWeapon.AnySlot` key: the module switch replaces it, and the
  erase-rate correction works differently - see `docs/Fixed-or-Improved-Logics.md`.)
- Shared per-techno platform extension `TechnoDataExt` (`src/Ext/TechnoDataExt.h`,
  allocated in `TechnoClass::Init` and removed in the base destructor).  It is
  the one TechnoClass extension of the platform, so every per-techno feature
  keeps its fields there instead of declaring a competing store.
- `docs/Fixed-or-Improved-Logics.md` section, `docs/EngineBindings.md` extension
  and ABI notes, four new rows in `docs/HookCatalogue.md` (27 hook records,
  25 distinct handler names), and the runtime ABI assertions the feature depends
  on in `src/Engine/EngineCompat.h`.

- **ROF curve** (`[Features] ROFCurve`, default on; the behaviour itself is
  opt-in per weapon) - ported from Phobos-LWModified commits `47023891` /
  `b950ac50` (the curve) and `6c54bd6f` (one chain per weapon).  A weapon that
  writes `ROFCurve=yes` and `ROFCurve.End` in its own rules section fires faster
  (or slower) the longer it keeps firing: `Start` -> `End` over `Ramp` frames,
  eased by `Power`, falling back over `Decay` after a pause and restarting at the
  top with `Cycle`.  Only the interval between two shots changes; the curve is
  applied as a ratio to the delay the engine computed, so AttachEffect ROF
  multipliers, veterancy and `ROF.RandomDelay` keep working.  Every weapon keeps
  its own spin-up chain (a multi-weapon techno's weapons never reset each other),
  the chains are transient (never saved), and the module hooks the two
  `Fire_At` reload sites (`0x6FF2AA` / `0x6FE4B0`) plus the two weapon-type INI
  epilogues (`0x7729B0` / `0x7729C7`, chained with a co-loaded official Phobos /
  Ares).  Unlike the fork, the `DiskLaser=yes` branch is handled correctly (the
  platform reads the weapon from the `Fire_At` argument instead of the register
  that holds the `DiskLaserClass*` on that branch).  **No savegame change.**
  See `docs/New-or-Enhanced-Logics.md` - the platform's first bilingual
  (English + 中文) feature document, including a section on how this version
  differs from the Phobos-LWModified one (switch, cooldown consumers,
  `DiskLaser=yes` branch, savegame, log).
- `docs/New-or-Enhanced-Logics.md` (first "new logic" document of the platform,
  written as **English + complete Chinese translation** in the same file) and
  `tests/test_rof_curve.cpp` (17 test programs now; the curve shape and the
  per-weapon state machine are covered without a game process - the math is in
  the engine-free `src/Features/ROFCurve/ROFCurveMath.h`).

- **Armed engineers** (`[Features] ArmedEngineers`, default on) - ported from
  Phobos-LWModified commit `c2341b52`.  An `Engineer=yes` infantry that carries a
  real weapon can attack enemy units like any other armed infantry (vanilla
  hardcodes `Engineer && Attack -> NoMove` at `0x51E6B4`), and Ctrl force-fire on
  a building attacks it instead of capturing it.  Without Ctrl a building click
  keeps the vanilla capture / repair / enter actions, Ivan-bomb disarm keeps its
  own cursor and non-engineer infantry are untouched.  Two layers: a hook at
  `0x51E6C4` releases the engineer attack clamp, and a feature-level **vtable
  slot takeover** of `InfantryClass::MouseOverObject` (`0x7EB0CC`, vanilla
  `0x51E3B0`) rewrites the force-fired building result - the slot lives in
  read-only `.rdata`, so the platform writes it through `VirtualProtect`, reads
  the previous value after every host DLL has patched (under Ares + official
  Phobos that is the Phobos wrapper) and chains to it.  This is the platform's
  first vtable binding, documented in `docs/EngineBindings.md`.  **No INI keys
  (no tuning needed); no savegame change.**  (Behaviour trade-off carried over
  from the fork: Ctrl means attack for any building, allied included; AI attack
  scripts still convert engineers to capture missions - manual attacks only.)

- **Tactical view zoom** (`[Features] ViewZoom`, default on) - ported from
  Phobos-LWModified commit `51fa3628`, with the follow-up fixes `8c43bd3f` /
  `ff77ea04` / `65043b38` / `a88c5d48` / `b62f46ce` / `70c86561`.  The tactical
  map is rendered at a resolution of its own: it can be zoomed in (`k < 1`,
  magnifying a centred crop of the native viewport - no extra rendering) and out
  (`k > 1`, really rendering a larger viewport and scaling the frame back down)
  while the sidebar, fonts, buttons and menus keep the resolution the game is set
  to.  Three commands in **Options -> Keyboard** under **VentureLW**
  (`Venture View Zoom In` / `Venture View Zoom Out` / `Venture Reset View Zoom`)
  plus a middle-click reset; zoom is per-match state, every match starts at the
  native level and the level is never saved.  **22 engine hooks**, **15
  `ViewZoom.*` keys** in the `[VentureLW]` section of `VentureLW.ini`,
  **0 savegame changes**.  Platform differences from the fork: the keys move from
  its `[Phobos]` section into the platform section (the names are unchanged),
  `[Features] ViewZoom` gates the module on top of the old `ViewZoom.Enable` key
  (both must be true), the INI is read once at process start (restart the game;
  the fork reloads on the options screen), the integer keys are range-validated
  (`FullRedrawEvery` `-1..600`, `MaxStaticSkip` `0..600`, out of range falls back
  to the default with a warning), `ViewZoom.WideView=false` leaves the zoom-out
  command unregistered, there is no CSF localisation (the fork's
  `TXT_VIEW_ZOOM_*` keys do not apply), and a fork bug that dropped the whole-map
  level when `full` was written explicitly is fixed.  A co-installed platform
  that implements the same tactical zoom is a feature-level conflict: the
  platform does not detect it and the two are not meant to run together - pick
  one via the switches.  See `docs/User-Interface.md` (English + 中英对照).

### Changed

- **Licensing statement corrected; no code change.**  The repository is now
  GPL-3.0 only at the licence level: `LICENSE.md` carries the GPL-3.0 text (like
  `COPYING`) and there is no separate MIT licence file - the VentureCore base's
  MIT notice is reproduced verbatim in `NOTICE.md`, where MIT requires it to
  travel with the code.  The YRpp wording no longer claims
  "no license declaration": one pinned header, `QueueClass.h`, is
  GPL-3.0-or-later (TS++, CCHyper / ZivDero) and the event sender instantiates
  its queue `Add()` - lawful because this platform is GPL-3.0.  The platform
  stays on the v2.1.0 base and does not take the 2.1.1 queue rewrite.  The built
  DLL, its behaviour, INI keys, hooks and savegame format are unchanged.
- The base **Example** module no longer owns a feature-level `UnitClass`
  extension store: a game object carries exactly one platform extension, and the
  new shared `TechnoDataExt` extends every techno (units included).  The module
  keeps its hooks, switch and logging as the smoke test, and the extension /
  savegame machinery stays covered by the shared store and the test suite.

### Fixed

- The shared per-techno extension's `TechnoClass::Init` hook (`0x6F42E3`)
  declared a 7-byte window over the engine's 10-byte
  `mov dword [esi+0x49c],1`.  SyringeEx re-executes the bytes a `return 0` hook
  covers, so the truncated window replayed malformed code and the game exited on
  the first techno construction (the first build of this batch flashed and
  closed right after loading a scenario).  The window now spans the whole
  instruction (size 0xA, next instruction `0x6F42ED`); `docs/HookCatalogue.md`
  and `docs/EngineBindings.md` were corrected with it.
- The offline hook-window validator (`scripts/verify_hook_windows.py`) mis-parsed
  objdump's continuation lines for instructions longer than seven bytes: the
  continuation was recorded as an instruction of its own, so the truncated
  window above passed validation (31 of 32 windows were valid, the report said
  32/32).  The parser now folds continuation bytes into the instruction they
  belong to, and a new offline `--self-check` (run by `scripts/verify_build.py`)
  pins both the parsing and the window-tiling behaviour down.

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
