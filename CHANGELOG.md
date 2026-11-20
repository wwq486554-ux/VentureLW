# Changelog

All notable changes to VentureLW are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses semantic versioning.  VentureLW keeps its own version line,
independent of the base it was copied from (see `README.md`).  The history of the
MIT-licensed VentureCore base is kept in that repository's `CHANGELOG.md`.

## [Unreleased]

### Added

- **Scatter core: burst-point scatter for engine-guided missiles.**  A projectile
  with `ROT > 0` or `Arm > 0` is steered by the engine to the centre of the live
  target on every frame, so neither engine scatter path can displace where it
  goes off.  A projectile that is also `Inaccurate=yes` and writes both
  `LW.BallisticScatter.Min` and `.Max` now gets its final burst point displaced
  with the same recipe (`Min` / `Max` at full range, `Sigma`, the `Divergence`
  ramp of the distance actually flown, `Chance`, the area-preserving `Aspect`
  ellipse along the flight line).  The base is the engine's own burst point, so a
  `Ranged` early detonation stays where it happened and a `Cluster` projectile
  keeps the engine's cluster scatter on top of the displaced centre.  This is the
  only path of the module that draws random numbers: two draws on the detonation
  frame from the synchronised scenario RNG, so both ends of a multiplayer match
  compute the same point (the battle's random-number stream differs from vanilla
  for the projectiles that use it).  The engine's own convergence point
  `0x468FF4` is occupied by the official Phobos / Mix / fork handlers, so the
  platform hooks the `Inaccurate` branch's `jne` at `0x468DCF/6` instead.  The
  projectile still flies to the target centre - only the warhead explodes
  off-centre.  See `docs/New-or-Enhanced-Logics.md` ("The burst point") and
  `docs/HookCatalogue.md`.

### Fixed

- **SuddenDeath: low bridges (the overlay bridge families).**  The tile-type test
  below only covers the high-bridge (iso-tile) system: a low bridge is an overlay
  on the water and carries neither the bridge cell flags nor the `BridgeMiddle`
  tile types, so the sweep never asked the engine to destroy it.  The engine's
  low-bridge branch is the other half of `MapClass::DamageArea`'s bridge section
  (`0x48A217`: `OverlayTypeIndex` `74..99`, `0x48A26D`: `205..230`), and the
  sweep now mirrors those two ranges (`IsEngineLowBridgeOverlayIndex` in
  `Features/SuddenDeath/BridgeTiles.h`) and sends the cell through the same
  `IonCannonWarhead` call.  The engine removes the whole overlay run itself, so
  this path does not run `BlowUpBridge` (the high-bridge aftermath).  The ranges
  are raw `[OverlayTypes]` indices - exactly what the engine tests - so the
  family tail past slot 99 stays outside the trigger for this feature just as it
  does for conventional weapons.  `tests/test_sudden_death_bridge_tiles.cpp` pins
  the ranges.
- **SuddenDeath: bridges on maps whose cells do not carry the bridge flags.**
  pbsnew8's trigger (`ContainsBridge() || ContainsBridgeEx()`) is kept, and the
  sweep now also mirrors the engine's own tile-type test from
  `MapClass::DamageArea` (`0x489EC9`: the cell's `IsoTileTypeIndex` against the
  `[General]` `BridgeMiddle1` / `BridgeMiddle2` groups, in
  `Features/SuddenDeath/BridgeTiles.h`).  The original-YR test map's bridges are
  tile-type-only, so the ported condition alone found no bridge cell even though
  conventional shells collapse the same bridge through the engine branch.  The
  rest of the pbsnew8 recipe (Z + 416 on a `BridgeHead` cell, the
  `IonCannonWarhead` `DamageArea` call, `BlowUpBridge`, de-duplication) is
  untouched.  `tests/test_sudden_death_bridge_tiles.cpp` pins the comparison.
- **SuddenDeath: the bridge repair hut of the original-YR rules.**  `[CABHUT]`
  is both `BridgeRepairHut=yes` and `Immune=yes`, and the engine returns from
  `BuildingClass::ReceiveDamage` for that combination before
  `TechnoClass::ReceiveDamage`, so the hit was never recorded.  A ninth hook
  (`0x4423A3`, size 6) lets a SuddenDeath warhead jump over the pair; the
  ordinary hit is still nullified by the engine's immunity, and the next-frame
  execution bypasses it with `IgnoreDefenses`.  That execution uses the neutral
  C4Warhead, so it would take the same building-level early-out again;
  `ExecutePending` marks the target and the hook lets that single call through
  too (the 2026-11-20 second test showed the hit recorded but the health
  unchanged before this half of the fix).  (The Mobile-Origin rules the fork was
  tested with are not `Immune`, so the fork never needed this site.)
- **SuddenDeath: the 0x701900 argument block.**  The first build used
  `GET_STACK` on the by-value `args_ReceiveDamage` block, i.e. it dereferenced
  the damage pointer and never matched a warhead - the feature looked inert in
  the 2026-11-20 real-machine test.  The access now goes through
  `Features/SuddenDeath/ReceiveDamageArgs.h` (`LEA_STACK`), pinned by
  `tests/test_sudden_death_args.cpp`.
- **SuddenDeath bridge diagnostics.**  With `[Platform] LogLevel=debug` the log
  now shows each recorded detonation (`detonation cell [...]`, including how
  many bridge cells lie inside and just outside the sweep radius and the
  scenario's `DestroyableBridges` flag), each bridge
  candidate with its cell flags / overlay / level / kind (high or low) / Z, and
  the `MapClass::DamageArea` result of the collapse attempt.

### Added

- **Universal scatter shaping** (`[Features] Scatter`, default on; the behaviour
  itself is opt-in per projectile) - the platform's own scatter system, keyed by
  `LW.BallisticScatter.*` in the projectile's own section of `rules(md).ini`.  A
  projectile that writes any key of the family is shaped; one that writes none
  keeps the host behaviour bit for bit.  The keys are `Min` / `Max` (cells,
  x256 = leptons, the radius at full range), `Sigma` (default 0.5 = area-uniform,
  1.0 = the engine's radial-uniform), `Aspect` (area-preserving true ellipse,
  default 1), `Divergence` (distance ramp; absent = `[General] LW.Divergence`,
  default 0.5) and `Chance` (default 1, decided from the host's own draw so no
  extra random number is consumed).  The radius is remapped on both `Fire_At`
  branches, the true ellipse is applied at the scatter block's exit (`0x6FE8EE`)
  before the facing / velocity / firing solution are derived, and the classic
  `FlakScatter` + `Inviso` `BulletClass::MoveTo` path is radius-shaped only (its
  post-scatter sites are owned by Ares / the official Phobos / Mix, so the 2-D
  ellipse cannot run there).  `IncreaseByRange` and its six range keys are read
  but **not applied** in this build (a projectile that writes them keeps the host
  scatter, with a warning); `Physical` (B16) and the burst-point scatter arrive
  later in the round.  Seven hooks - the probe's four (`0x6FE67D` / `0x6FE71C` /
  `0x6FE821` / `0x6FE8EE`) plus three new sites (`0x6FE73F` / `0x4687F8` /
  `0x46C41C`), catalogue 67 -> 70 records (63 -> 66 distinct handler names).
  **No savegame change and no extra random draw** - the configuration is a
  feature-local table filled when the rules are parsed, and the RNG stream stays
  the host's.  See `docs/New-or-Enhanced-Logics.md` (English + 中英对照) and
  `tests/test_scatter_math.cpp` (23 test programs now; the engine-free geometry
  in `src/Features/Scatter/ScatterMath.h` is covered without a game process).

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
  targets and may fire at a 0% `Verses` target.  Nine new hooks
  (`0x701900` / `0x75DEA0` / `0x55B4E1` / `0x6FCB64` / `0x417F63` / `0x51F179` /
  `0x74049A` / `0x4423A3` / `0x489286`), catalogue 54 -> 63.  **No platform INI keys and no
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
