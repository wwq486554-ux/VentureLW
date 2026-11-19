# NOTICE

VentureLW is a GPL-3.0 platform DLL for *Command & Conquer: Yuri's Revenge*,
built on the MIT-licensed VentureCore v2.1.0 starter base.  This file records
the licenses and third-party components the build and the runtime touch, and
what is (and is not) distributed here.

## Summary

| Component | Distributed here | License / terms | Notes |
| --- | --- | --- | --- |
| VentureLW platform code and feature modules | yes | GPL-3.0 (`COPYING`) | Copyright (c) 2026 the VentureLW contributors |
| VentureCore base source | yes | MIT notice retained (reproduced below) | Copyright (c) 2026 The VentureCore Contributors; the notice travels with the base code because MIT requires it |
| `gamemd.exe` and game assets | no | proprietary (EA / Westwood) | the user must own a copy of Yuri's Revenge 1.001 |
| YRpp (engine wrapper headers) | no | no repository-level licence; `QueueClass.h` is GPL-3.0-or-later and is instantiated | external build dependency, fetched locally, pinned by commit; the one copyleft file is compiled in, which this platform's GPL-3.0 covers |
| SyringeEx (`Syringe.exe`) | no | not covered; interface format only | used at run time; only the documented section format is used |
| Ares | no | per its own project | optional host, detected at run time only |
| Phobos | no | per its own project | optional host, detected at run time only |
| Kratos | no | per its own project | optional host, detected at run time only |

## VentureCore base (MIT notice)

The base machinery (startup, logging, INI layer, extensions, savegame trailer,
events, commands, lineage, hosts, build/verification scripts) was taken from the
VentureCore v2.1.0 repository under the MIT license.  VentureLW as a whole is
distributed under GPL-3.0 (`LICENSE.md`, `COPYING`); the notice below is the base
authors' notice, retained verbatim because MIT requires it to accompany copies of
their code.  It covers the base files only - not the feature modules under
`src/Features/` - and it is a third-party notice, not an alternative licence for
this repository.

```text
MIT License

Copyright (c) 2026 The VentureCore Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## YRpp

`external/YRpp/` is the YRpp engine wrapper that provides the C++ view of
`gamemd.exe`.  **Repository level: no license file and no stated terms** - the
community uses the headers in practice without a formal grant.  **File level:
one header is copyleft.**  In the pinned revision `c6c522f7` exactly one of 337
source files carries an explicit notice: `QueueClass.h` is **GPL-3.0-or-later**
(from TS++, credited there to CCHyper and ZivDero) and defines every queue
member function **inline in the header**.

This platform **does instantiate that header.**  The event sender in
`src/Engine/Hooks.Events.cpp` queues a frame with `EventClass::OutList.Add(...)`,
so the compiled DLL contains that implementation.  That is lawful here because
**VentureLW as a whole is GPL-3.0** (`COPYING`, `LICENSE.md`): GPL-3.0-or-later
code may be distributed as part of a GPL-3.0 work.  The source of that
implementation is the pinned YRpp revision
`c6c522f7b58508588d9a8652c53356e4aef7a96` - public, and fetched locally by
`scripts/linux/fetch_yrpp.sh`; it is not copied into this repository.

The rest of the dependency is handled as before:

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

The remaining 336 files state no terms at all.  That is an ecosystem-wide
position - the official Phobos compiles against the same tree - and this
repository records it instead of claiming a permission it does not have.  Unlike
the VentureCore base at v2.1.1, the platform does not replace the queue call:
staying GPL-3.0 is what keeps it lawful, and the platform is not upgraded to the
2.1.1 base.

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
