# Licensing

VentureLW is distributed under **GPL-3.0** (see `COPYING`): it is the
MIT-licensed VentureCore base plus GPL-3.0 feature modules ported from the
Phobos-LWModified fork.  This document records what each part is under.

## Summary

| Component | License | Distributed here? |
| --- | --- | --- |
| VentureLW platform code and feature modules | GPL-3.0 (`COPYING`) | yes |
| VentureCore base source | MIT (`LICENSE.md`) | yes, MIT notice retained |
| Ported feature modules (from Phobos-LWModified) | GPL-3.0 | yes, under `src/Features/` |
| YRpp (engine wrapper headers) | *no license declaration* - not redistributed | no |
| SyringeEx (`Syringe.exe`) | not covered; not redistributed | no |
| Ares / Phobos / Kratos | their own licenses; not redistributed | no |
| `gamemd.exe` and game assets | proprietary (EA / Westwood) | no |

## Why GPL-3.0

The base is MIT and the gameplay modules are ported from a GPL-3.0 project: MIT
code may be combined into a GPL-3.0 work, but GPL-3.0 code cannot be relicensed
as MIT.  The combined work is therefore GPL-3.0.

* `LICENSE.md` (MIT, Copyright (c) 2026 The VentureCore Contributors) is kept
  verbatim because the base is a substantial part of this work.  Keep it in any
  redistribution, together with `NOTICE.md`.
* `COPYING` is the GPL-3.0 text that applies to VentureLW's own additions and to
  the combined distribution.
* `CREDITS.md` records the authorship of the ported modules.

## YRpp is not covered by either license

YRpp is the C++ wrapper around `gamemd.exe` that the platform compiles against.
It has **no license declaration**; the community uses it in practice but there
is no formal grant.  YRpp is **not** distributed in this repository
(`external/YRpp/` is gitignored), it is an external build dependency pinned by
commit in `src/Core/EnginePin.h` and fetched with
`scripts/linux/fetch_yrpp.sh`.  Neither the MIT grant nor the GPL grant covers
YRpp or makes any statement about it.

## SyringeEx and the optional hosts

* **SyringeEx** is used at run time and as an interface definition (the
  `.syhks00` / `.syexe00` sections).  No SyringeEx headers or source are copied;
  the hook macros come from the external YRpp `Syringe.h`.
* **Ares / Phobos / Kratos** are optional hosts.  The platform only detects
  their loaded modules and logs the result; nothing is copied, linked or
  vendored.  Ares and Phobos are read as behavioural reference when porting a
  fork feature (their own licenses apply to them).

## The game

`gamemd.exe`, Yuri's Revenge and its assets are proprietary property of their
respective rights holders.  VentureLW requires a legitimate installation and
distributes no game data.  Nothing here grants rights to the game.

## Inbound contributions

Contributions to this repository are accepted under GPL-3.0 (inbound = outbound),
unless a contribution file says otherwise.

## The base's own guidance

The inherited base document `docs/` still describes the MIT base template and how
a derived platform may choose its own license.  That guidance applies to a
platform built from the base alone; VentureLW itself is GPL-3.0 because it
contains the ported modules.
