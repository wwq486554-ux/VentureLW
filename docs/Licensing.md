# Licensing

VentureLW is distributed under **GPL-3.0** (see `LICENSE.md` / `COPYING`): it
is the MIT-licensed VentureCore base plus GPL-3.0 feature modules ported from
the Phobos-LWModified fork.  This document records what each part is under.

## Summary

| Component | License | Distributed here? |
| --- | --- | --- |
| VentureLW platform code and feature modules | GPL-3.0 (`COPYING`) | yes |
| VentureCore base source | MIT notice retained in `NOTICE.md` | yes, notice retained |
| Ported feature modules (from Phobos-LWModified) | GPL-3.0 | yes, under `src/Features/` |
| YRpp (engine wrapper headers) | no repository-level licence; one header (`QueueClass.h`) is GPL-3.0-or-later and is instantiated | no - external, pinned by commit |
| SyringeEx (`Syringe.exe`) | not covered; not redistributed | no |
| Ares / Phobos / Kratos | their own licenses; not redistributed | no |
| `gamemd.exe` and game assets | proprietary (EA / Westwood) | no |

## Why GPL-3.0

The base is MIT and the gameplay modules are ported from a GPL-3.0 project: MIT
code may be combined into a GPL-3.0 work, but GPL-3.0 code cannot be relicensed
as MIT.  The combined work is therefore GPL-3.0.

* `NOTICE.md` reproduces the base's MIT notice (Copyright (c) 2026 The
  VentureCore Contributors) verbatim because the base is a substantial part of
  this work.  Keep `NOTICE.md` in any redistribution; the MIT text there is a
  required third-party notice, not an alternative licence.
* `LICENSE.md` and `COPYING` both carry the GPL-3.0 text that applies to the
  whole work; the per-component split is stated here and in `NOTICE.md`.
* `CREDITS.md` records the authorship of the ported modules.

## YRpp is not covered by either license

YRpp is the C++ wrapper around `gamemd.exe` that the platform compiles against.
**At the repository level it has no licence file and states no terms**; the
community uses it in practice without a formal grant.  **At the file level one
header is copyleft:** `QueueClass.h` - from TS++, credited there to CCHyper and
ZivDero - carries a **GPL-3.0-or-later** notice and defines every queue member
inline, so a call to one of them compiles that implementation into the calling
DLL.

VentureLW's event sender does make such a call: `EventClass::OutList.Add(...)`
in `src/Engine/Hooks.Events.cpp`.  That is **compatible with this platform's
licence** - the platform as a whole is GPL-3.0, and GPL-3.0-or-later code may be
distributed as part of a GPL-3.0 work (GPLv3 section 14 lets the recipient
follow version 3).  The source of that implementation is the pinned YRpp
revision `c6c522f7...`; it is public and fetched by
`scripts/linux/fetch_yrpp.sh`, not redistributed here.

Neither grant covers YRpp's other files; they state no terms at all.  That is an
ecosystem-wide position (the official Phobos compiles against the same tree),
not something this repository can resolve.

**If this platform's licence ever changes**, or a platform is derived from it,
the queue call is one of the blocking points: keep the work GPL-3.0, or replace
the call with an own append against the engine queue layout - which is what the
VentureCore 2.1.1 base does.  This platform chooses the former and stays on the
2.1.0 base.  Because the repository predates the base's `check_copyleft.py`
gate, a YRpp pin update must re-check `QueueClass.h` by hand
(`docs/Updating.md`).

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

## The inherited base guidance

The base documents under `docs/` are inherited from the MIT starter base and
still describe how a platform built from the base alone may choose its own
licence.  That guidance does not apply to VentureLW: this repository as a whole
is GPL-3.0 because of the ported modules, and anything derived from it must stay
GPL-3.0.  `docs/MakeYourOwnPlatform.md` carries a note saying so.
