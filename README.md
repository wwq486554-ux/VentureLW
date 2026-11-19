# VentureLW

VentureLW is a **GPL-3.0 platform DLL** for *Command & Conquer: Yuri's Revenge*
1.001, injected with **SyringeEx**.  It is built on the MIT-licensed
**VentureCore v2.1.0** starter base plus our own feature modules, which are
ported from the **Phobos-LWModified** fork.

> **Status: v0.1.0 (bootstrap, 2026-11-14)** - the first build of the platform:
> the identity was renamed from the base, the platform has its own savegame
> trailer id, and the first gameplay features have been migrated (large-range
> radial indicator drawing, Temporal warheads from any weapon slot, the ROF
> curve, armed engineers and the tactical view zoom).  It is not a
> feature-complete mod yet.

## What this platform provides

Base machinery, inherited from VentureCore 2.1.0 and documented under `docs/`:
startup pipeline, file logging, the `<Platform>.ini` key registry, extension
storage with savegame persistence, the event channel, command registration,
lineage mutual exclusion, the host detection set, the YRpp binding gate, and the
build/verification tooling.

Feature modules (`src/Features/`), registered as one `[Features]` switch each:

| Module | Switch | What it does |
| --- | --- | --- |
| `RadialIndicator` | `[Features] RadialIndicator` (default on) | Draws the radial indicator's two concentric ellipse outlines with double-precision scan lines once the engine's 32-bit ellipse rasterizer would overflow, so selected-unit / placement-preview / superweapon-targeting rings stay visible at long ranges instead of disappearing.  Rendering only; no INI keys, no savegame change.  See `docs/Fixed-or-Improved-Logics.md`. |
| `TemporalAnySlot` | `[Features] TemporalAnySlot` (default on) | Creates the techno's Temporal instance when a Temporal warhead arrives from a weapon the engine did not create one for (the engine only checks weapon slot 0 and then dereferences the missing instance - the known vanilla crash at `0x71AF4D`), and erases a running warp at the rate of the weapon that actually fired the warhead.  No INI keys, no savegame change.  See `docs/Fixed-or-Improved-Logics.md`. |
| `ArmedEngineers` | `[Features] ArmedEngineers` (default on) | An `Engineer=yes` infantry that carries a real weapon can attack enemy units like any other armed infantry, and Ctrl force-fire on a building attacks it instead of capturing it.  Two layers: the engineer attack-clamp hook (`0x51E6C4`) and a feature-level vtable takeover of `InfantryClass::MouseOverObject` (`0x7EB0CC`, chained to the previous implementation).  No INI keys, no savegame change.  See `docs/Fixed-or-Improved-Logics.md` and `docs/EngineBindings.md`. |
| `ROFCurve` | `[Features] ROFCurve` (default on, the behaviour itself is opt-in per weapon) | Weapon-level spin-up: with `ROFCurve=yes` and `ROFCurve.End` in a weapon section, that weapon fires faster (or slower) the longer it keeps firing; one chain per weapon, so a techno's weapons never reset each other's progress.  Seven `ROFCurve.*` weapon keys, no savegame change.  See `docs/New-or-Enhanced-Logics.md` (English + 中英对照). |
| `ViewZoom` | `[Features] ViewZoom` (default on) | Tactical map zoom independent of the UI resolution: zooming in magnifies a centred crop of the native viewport (no extra rendering), zooming out renders a larger viewport and scales the frame back down, so units and terrain shrink together while the sidebar, fonts and buttons keep their size.  Three commands in **Options -> Keyboard** under **VentureLW** (`Zoom In (Tactical View)`, `Zoom Out (Tactical View)`, `Reset Tactical View Zoom`) plus middle-click reset; every match starts at the native level.  Fifteen `ViewZoom.*` keys in the `[VentureLW]` section (`ViewZoom.WideView` gates the zoom-out direction), 22 hooks, no savegame change.  See `docs/User-Interface.md` (English + 中英对照). |

The stock `Example` / `ExampleIniKey` modules of the base are kept: they have no
gameplay effect and serve as the base's own smoke tests.

## Building

Linux, with the real MSVC toolchain under Wine:

```bash
export VCS_WINEPREFIX=<writable Wine prefix on a Linux-native filesystem>
scripts/linux/build_linux.sh Release --build-type RELEASE -j 8
```

Windows: open `VentureLW.sln` in Visual Studio 2022 (v143).

YRpp is an external build dependency and is **not** vendored; install the pinned
checkout once with `scripts/linux/fetch_yrpp.sh --from <path-to-YRpp>` (the pin
lives in `src/Core/EnginePin.h`).

## Verifying a build

```bash
python3 scripts/verify_build.py        # PE, hooks vs docs/HookCatalogue.md, exports, version resource
scripts/linux/run_tests.sh             # engine-free unit tests
scripts/linux/load_probe.sh            # loads the DLL under Wine, checks exports + lineage exclusion
python3 scripts/linux/check_eol.py     # line-ending consistency
```

None of these replaces the in-game check: drop `VentureLW.dll` (only one
VentureCore-lineage DLL per game directory) next to `gamemd.exe`, start the game
through `SyringeEx.exe` and inspect `VentureLW.log` (version, hosts,
`startup complete`) plus the feature behaviour.

## Installing

1. Copy `VentureLW.dll` into the game directory (next to `gamemd.exe`).
2. Optional: copy `VentureLW.ini.example` to `VentureLW.ini` there and edit it.
3. Launch through `SyringeEx.exe`.

Do **not** install two DLLs of this lineage (VentureCore / VentureLW) in the same
game directory: the second one is detected and refused by design (see
`docs/Lineage.md`).

## Repository layout

| Path | Contents |
| --- | --- |
| `src/Core/` | platform machinery: identity, startup, logging, INI layer, extensions, savegame trailer, lineage, host registry |
| `src/Engine/` | base engine hooks: lifecycle, savegame tail, events, commands, INI takeover |
| `src/Features/` | one directory per feature module |
| `include/platform/` | the source-level feature API (`FeatureDescriptor`, `FeatureContext`, ...) |
| `docs/` | base documentation and the feature documentation |
| `scripts/` | build, verification and probe tooling |
| `tests/` | engine-free unit tests |
| `external/YRpp/` | pinned YRpp headers, fetched locally (not committed) |

## Licensing

* **VentureLW as a whole** is **GPL-3.0** - `LICENSE.md` and `COPYING` carry
  the full licence text; the VentureCore base's MIT notice is reproduced in
  `NOTICE.md`.
* The **VentureCore base** this platform is built from is **MIT**; the notice is
  reproduced verbatim in `NOTICE.md` and retained because MIT requires it with
  copies of the code.  MIT code may be combined into a GPL-3.0 work - that
  notice is not an alternative licence for this repository.
* YRpp has **no repository-level licence** and is **not** distributed here
  (external build dependency).  One header, `QueueClass.h`, is
  **GPL-3.0-or-later** (TS++, CCHyper / ZivDero) and this platform instantiates
  its event-queue `Add()`; that is lawful because the platform itself is
  GPL-3.0.  Ares / Phobos / Kratos are optional hosts, never redistributed.
  `gamemd.exe` and game assets remain proprietary.
* Third-party attribution for ported code lives in `CREDITS.md`.

## Relationship to the base and to the fork

* Base: [`VentureCore`](https://github.com/wwq486554-ux/VentureCore) v2.1.0
  (MIT starter base; its own CHANGELOG is the base history).  The platform stays
  on v2.1.0 and does not take the 2.1.1 queue rewrite: it is GPL-3.0, so
  instantiating the copyleft queue header is lawful (see `docs/Licensing.md`).
* Gameplay source: `Phobos-LWModified`, the Phobos fork the feature modules are
  ported from (GPL-3.0, attribution in `CREDITS.md`).
* The base documents under `docs/` are inherited and describe the shared
  machinery; `docs/Fixed-or-Improved-Logics.md`,
  `docs/New-or-Enhanced-Logics.md` and `docs/User-Interface.md` describe what
  VentureLW adds on top, and `CREDITS.md` holds the attribution.
