# VentureLW

VentureLW is a **GPL-3.0 platform DLL** for *Command & Conquer: Yuri's Revenge*
1.001, injected with **SyringeEx**.  It is built on the MIT-licensed
**VentureCore v2.1.0** starter base plus our own feature modules, which are
ported from the **Phobos-LWModified** fork.

> **Status: v0.1.0 (bootstrap, 2026-11-14)** - the first build of the platform:
> the identity was renamed from the base and the platform has its own savegame
> trailer id.  No gameplay feature module has been migrated yet; it is not a
> feature-complete mod.

## What this platform provides

Base machinery, inherited from VentureCore 2.1.0 and documented under `docs/`:
startup pipeline, file logging, the `<Platform>.ini` key registry, extension
storage with savegame persistence, the event channel, command registration,
lineage mutual exclusion, the host detection set, the YRpp binding gate, and the
build/verification tooling.

Feature modules (`src/Features/`), registered as one `[Features]` switch each:

None yet in this bootstrap commit - the first module (`RadialIndicator`) lands
in the next commit.

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

* **VentureLW** (the platform assembly and the feature modules added here) is
  **GPL-3.0** - see `COPYING`.
* The **VentureCore base** this platform is built from is **MIT** - the original
  grant is kept verbatim in `LICENSE.md` and recorded in `NOTICE.md`.  MIT code
  may be combined into a GPL-3.0 work; the MIT notice is retained.
* YRpp carries no license declaration and is **not** distributed here (external
  build dependency).  Ares / Phobos / Kratos are optional hosts, never
  redistributed.  `gamemd.exe` and game assets remain proprietary.
* Third-party attribution for ported code lives in `CREDITS.md`.

## Relationship to the base and to the fork

* Base: [`VentureCore`](https://github.com/wwq486554-ux/VentureCore) v2.1.0
  (MIT starter base; its own CHANGELOG is the base history).
* Gameplay source: `Phobos-LWModified`, the Phobos fork the feature modules are
  ported from (GPL-3.0, attribution in `CREDITS.md`).
* The base documents under `docs/` are inherited and describe the shared
  machinery; `docs/Fixed-or-Improved-Logics.md` and `CREDITS.md` describe what
  VentureLW adds on top.
