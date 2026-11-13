# NOTICE

VentureLW is a GPL-3.0 platform DLL for *Command & Conquer: Yuri's Revenge*,
built on the MIT-licensed VentureCore v2.1.0 starter base.  This file records
the licenses and third-party components the build and the runtime touch, and
what is (and is not) distributed here.

## Summary

| Component | Distributed here | License / terms | Notes |
| --- | --- | --- | --- |
| VentureLW platform code and feature modules | yes | GPL-3.0 (`COPYING`) | Copyright (c) 2026 the VentureLW contributors |
| VentureCore base source | yes | MIT (`LICENSE.md`) | Copyright (c) 2026 The VentureCore Contributors; MIT notice retained because the base is a substantial part of this work |
| `gamemd.exe` and game assets | no | proprietary (EA / Westwood) | the user must own a copy of Yuri's Revenge 1.001 |
| YRpp (engine wrapper headers) | no | no license declaration | external build dependency, fetched locally, pinned by commit |
| SyringeEx (`Syringe.exe`) | no | not covered; interface format only | used at run time; only the documented section format is used |
| Ares | no | per its own project | optional host, detected at run time only |
| Phobos | no | per its own project | optional host, detected at run time only |
| Kratos | no | per its own project | optional host, detected at run time only |

## VentureCore base (MIT)

The base machinery (startup, logging, INI layer, extensions, savegame trailer,
events, commands, lineage, hosts, build/verification scripts) was taken from the
VentureCore v2.1.0 repository under the MIT license in `LICENSE.md`.  The MIT
notice and this file must be kept when the work is redistributed.  VentureLW
adds its own feature modules under `src/Features/` and the platform identity
changes described in the CHANGELOG; those additions are GPL-3.0.

## YRpp

`external/YRpp/` is the YRpp engine wrapper that provides the C++ view of
`gamemd.exe`.  YRpp carries **no license declaration**; the community uses it in
practice without a formal grant.  For that reason:

* this repository does **not** redistribute YRpp - `external/YRpp/` and
  `external/YRpp.commit` are excluded by `.gitignore`.  The one committed file
  in that directory, `external/YRpp.sha256`, is a manifest of file hashes (no
  YRpp source) used to verify the local checkout, so it is not a redistribution
  of YRpp either;
* the local checkout is fetched with `scripts/linux/fetch_yrpp.sh` and pinned by
  commit in `src/Core/EnginePin.h` (`VC_YRPP_PIN`).  The build refuses to run
  against a different revision (`scripts/linux/check_yrpp.py`, plus
  `static_assert`s in `src/Engine/EngineCompat.h`);
* only public headers and `StaticInits.cpp` from that external checkout are
  compiled - no YRpp source is copied into this repository.

The MIT/GPL grants above therefore cover VentureLW's own code, not YRpp.

## SyringeEx

VentureLW is loaded by SyringeEx (the Syringe fork that scans the two sections
below) through `.syhks00` for hooks and `.syexe00` for host declarations.  The
section layout is used as a documented interface fact:

* no SyringeEx headers are copied into this repository;
* the `DEFINE_HOOK` / `DEFINE_HOOK_AGAIN` / `REGISTERS` macros and the
  `SyringeFeatures` flags come from the external YRpp `Syringe.h`, i.e. from the
  external build dependency, not from a vendored SyringeEx source tree;
* the platform refuses to start when it detects an older Syringe (the
  `SyringeFeatures` flags are false) instead of risking a corrupted process.

## Ares, Phobos, Kratos

These are optional host platforms.  VentureLW detects whether their modules are
loaded in the game process and logs the detected set (see `docs/HostMatrix.md`).
The platform has no code copied or vendored from any of them, no link
dependency and no functional dependency - they can be absent, present, or in any
combination.

## Feature modules ported from Phobos-LWModified

The gameplay feature modules are ported from the GPL-3.0 Phobos-LWModified fork;
that is what makes the combined work GPL-3.0.  Per-feature attribution (including
the upstream PR #2060 trajectory system: original work by CrimRecya, reopened by
TaranDahl) is recorded in `CREDITS.md`.  No code is copied from Ares, Phobos or
Kratos; they are only read as behavioural reference.

## Game and trademarks

`gamemd.exe`, Yuri's Revenge, Command & Conquer and related names and assets are
the property of their respective rights holders (Electronic Arts / Westwood
Studios).  Nothing in this repository grants any rights to them.  VentureLW
requires a legitimate installation of the game and does not distribute or
reproduce any game data.
