# Hook Catalogue

This file is the **single source of truth** for every engine hook the VentureLW
base installs.  `scripts/verify_build.py` parses the table below at run time and
requires the hook records embedded in the built DLL to match it exactly - there
is no second copy of the list in the verify script, and adding a hook to the
code without adding its row here (or the other way round) makes verification
fail.  When the engine executable is supplied as well (`VCS_GAMEMD_EXE=<path>
python3 scripts/verify_build.py`, or `--gamemd <path>`), the verifier additionally
re-decodes `gamemd.exe` and proves that every declared window starts on an
instruction boundary and covers complete instructions only (see "What size
means" below).

## Format rules

The table is machine-read.  Keep these rules when editing it:

* Only this one table may start with two `0x`-prefixed hexadecimal cells - the
  parser treats every such row as a hook record.  Use a bullet list or a table
  with a different first column for anything else.
* Column order is fixed: **Address | Size | Name | Purpose | Notes**.  The
  header row is `| 地址 | size | 名称 | 用途 | 备注 |` (kept in that form because
  the task specification and the parser both key on this column order); the
  rows below it are English.
* Address and size are hexadecimal with a `0x` prefix; sizes are the byte count
  that the hook window overwrites.
* The name cell must contain the hook handler identifier (a C identifier).
* Addresses are game-executable virtual addresses for the supported
  `gamemd.exe` build (Yuri's Revenge 1.001, as pinned by the build target).  A
  different executable build will not match these addresses.

## Current hook table

| 地址 | size | 名称 | 用途 | 备注 |
| --- | --- | --- | --- | --- |
| 0x7CD810 | 0x9 | Venture_ExeRun | Base initialization: SyringeEx gate, read the INI, build the host detection matrix, initialize feature modules, write the startup log | Core; runs once per process |
| 0x685659 | 0xa | Venture_ScenarioClear | Scenario cleanup: clear every extension store and the global extension index | Core; scenario teardown. NB: the engine also calls `ResetScenario` (this function) **inside** a savegame load (`0x67E730` -> `0x67E739`), after the load window opened at `0x67E44D`; the handler keeps the load window open in that case (resetting it would break the trailer adoption) |
| 0x7258D0 | 0x6 | Venture_AnnounceInvalidPointer | Pointer-invalidation notice: the engine announces that an owner pointer became invalid; the extension index counts it, drops a stale deferred allocation for that owner and keeps extension removal owned by the destructor hook | Core; there is no subscriber/log API for pointer-keyed feature caches yet - the counter is diagnostic only (L-01) |
| 0x7353D3 | 0x7 | Venture_Example_UnitClass_CTOR | Example module: count a `UnitClass` object on construction | Example module; smoke test for feature registration, the [Features] switch, hook installation and sampling logging (the module no longer owns a UnitClass extension - per-techno state lives in the shared `TechnoDataExt`, see docs/EngineBindings.md) |
| 0x735967 | 0x9 | Venture_Example_UnitClass_DTOR | Example module: count a `UnitClass` object on destruction | Example module; first destructor site |
| 0x7359DA | 0x9 | Venture_Example_UnitClass_DTOR | Example module: same handler at the second destructor site | Example module; declared with `DEFINE_HOOK_AGAIN` |
| 0x6686C0 | 0x6 | Venture_RulesClass_Init | INI takeover: the parsed rules(md).ini is available and not yet read - notify the rules-INI observers, then remove the platform-owned keys so a host platform such as Phobos (its `0x668BF0` hook is called from inside this function) cannot read them too | Core; objdump from `0x6686C0`: first instruction `sub esp,0xd0` (6 B) and `0x6686C6` is the next instruction - free site, no platform hook table uses it; `[Platform] IniTakeover=no` keeps the keys visible |
| 0x67E44D | 0x5 | Venture_LoadGame_Begin | Savegame load: open the extension load window, so allocations driven by object constructors are deferred until the trailer has been read | Save tail; first instruction `push 0x83d620` (5 B), `address + 5` is the next instruction; same site as the ecosystem's LoadGame_SetFlag hooks (all size 5) |
| 0x67D1B4 | 0x6 | Venture_SaveGame_Trailer | Savegame save: append the platform's framed tail block after the engine's object-state writer returned and before the engine finalizes/commits the storage | Save tail; window `mov bl,al`(2) + `mov eax,[esp+0x14]`(4) ends at 0x67D1BA (`cmp eax,ebp`); free site - none of the five audited ecosystem platform hook tables (Ares / Kratos / Mix / Phobos and one closed-source platform) uses it. The historical same-address tail site 0x67E42E is owned by Ares: its handler always returns a jump address and the site lies *inside* the 0x67D300 object-state writer, i.e. it runs before this site, so it cannot host our writer. The save stream `IStream*` is at [esp+0x1C] (loaded into ECX for `call 0x67D300` at 0x67D1AF); AL of EAX is 0x67D300's BOOL result (every failure epilogue runs `xor al,al` and leaves the negative HRESULT in the upper 24 bits, the success tail runs `setge al`; the engine itself consumes only AL) - AL == 0 means the save failed, do not append. The append is strictly sequential: the engine's savegame stream does not implement `Seek` (streaming wrapper around the OLE CONTENTS stream), and every ecosystem writer runs inside the `0x67D300` call (or at 0x67E42E inside it), so the current position is the end of the save data and our block is the last one |
| 0x67F7C8 | 0x5 | Venture_LoadGame_Trailer | Savegame load: read the framed tail block sequentially from the current stream position (our handler runs after every ecosystem sequential reader, so that position is exactly our block) and validate it | Same site as the Ares / Kratos / Mix load-end hooks and one closed-source ecosystem hook (all size 5); window `pop edi`(1) + `pop esi`(1) + `pop ebp`(1) + `mov al,1`(2) ends at 0x67F7CD. No `Seek`/position restore: the engine's savegame stream is a non-seekable streaming wrapper and every platform reads its block sequentially; legal COM short reads are retried by the header/payload read loops |
| 0x67E685 | 0x5 | Venture_LoadGame_PostSwizzle | Savegame load: first instruction after the engine's swizzle pass - attach the restored extensions to their remapped owners | Window `call 0x685120` (5 B) ends on the next instruction; same site as the fork lineage's post-swizzle hook (size 5) |
| 0x67E68A | 0x5 | Venture_LoadGame_End | Savegame load: close the extension load window and resume normal allocation | Window `mov ecx,0x87f7e8` (5 B); same site as the Mix and one closed-source platform's load-window-close hooks (size 5); idempotent second close - the post-swizzle handler already closes the window on the success path, this site remains the documented close point |
| 0x67E4B1 | 0x5 | Venture_LoadGame_Failed1 | Savegame load: close the extension load window when the engine aborts the load (first `StgOpenStorage` failure) | Save tail; window `xor al,al`(2) + `pop edi`(1) + `pop esi`(1) + `pop ebp`(1) ends at 0x67E4B6 (`pop ebx`); free site - none of the five ecosystem platform hook tables uses it; the engine's normal close point (0x67E68A) is not reached on this path, so this is the only place the window is closed |
| 0x67E53A | 0x5 | Venture_LoadGame_Failed2 | Savegame load: same, for the second `StgOpenStorage` failure of the engine's load routine | Save tail; window `xor al,al`(2) + `pop edi`(1) + `pop esi`(1) + `pop ebp`(1) ends at 0x67E53F (`pop ebx`); free site; same failure-close purpose as Failed1 |
| 0x67E595 | 0x5 | Venture_LoadGame_Failed3 | Savegame load: same, for a failed COM method in the engine's load routine | Save tail; window `pop edi`(1) + `pop esi`(1) + `pop ebp`(1) + `xor al,al`(2) ends at 0x67E59A (`pop ebx`); free site; same failure-close purpose as Failed1 |
| 0x4C6CC8 | 0x5 | Venture_Event_Dispatch | Event channel: route the platform's own event types 0x70-0x7F to their registered responders; the engine's own event switch still runs afterwards | Event channel; same site and semantics as the reference ecosystem declares (Phobos/Kratos/Mix `.syhks00` tables: 0x4C6CC8, size 0x5, handler returns 0). objdump from 0x4C6CC8: `mov al,[esi]` (2 bytes) + `mov edi,[ecx+ebx*4]` (3 bytes), next instruction 0x4C6CCD = address+5; the size must stay 5 because 0x4C6CCD is another platform's patch window |
| 0x64B6FE | 0x6 | Venture_Event_GetSize1 | Event channel: return the registered payload size for our event types at the first GetEventSize site (type in EDI), then resume at 0x64B71D with EDX/EBP = size | Event channel; same site and semantics as the reference ecosystem declares (Phobos/Kratos/Mix: size 0x6). objdump from 0x64B6FE: `and edi,0xff` (6 bytes), next instruction 0x64B704 = address+6; the vanilla path reads the engine's 47-entry size table (`0x00-0x2E`), which our numbers are outside of |
| 0x64BE7D | 0x6 | Venture_Event_GetSize2 | Event channel: same as GetSize1 but also writes the size into the caller frame slot [esp+0x20] (STACK_OFFSET(0xAC, -0x8C)) and resumes at 0x64BE97 | Event channel; same site and semantics as the reference ecosystem declares (Phobos/Kratos/Mix: size 0x6, stack slot [esp+0x20]). objdump from 0x64BE7D: `and edi,0xff` (6 bytes), next instruction 0x64BE83 = address+6 |
| 0x64C30E | 0x6 | Venture_Event_GetSize3 | Event channel: same as GetSize1 with the type taken from ESI and resumes at 0x64C321 | Event channel; same site and semantics as the reference ecosystem declares (Phobos/Kratos/Mix: size 0x6). objdump from 0x64C30E: `and esi,0xff` (6 bytes), next instruction 0x64C314 = address+6 |
| 0x533066 | 0x6 | Venture_Commands_Register | Commands: create one engine command object per enabled platform command descriptor and append it to CommandClass::Array; the command then appears in the game's keyboard dialog where the player binds a key | Commands; same site and semantics as the reference ecosystem declares (the audited Phobos/Kratos/Mix `.syhks00` tables and one closed-source ecosystem platform: 0x533066, size 0x6, handler returns 0). objdump from 0x533066: `mov DWORD PTR [eax],0x7eb89c` (6 bytes), next instruction 0x53306C = address+6; returning 0 lets the trampoline re-execute that store, so the engine's own command object is still initialised |
| 0x533C3E | 0x5 | Venture_Commands_VerifyRegistered | Commands: after the engine's own command registration is complete, scan the final shared command list for name collisions that involve a platform command and warn with the real first-match direction - both directions, covering engine commands (and other DLLs' commands) registered before *and* after the platform's 0x533066 entry | Commands; free site - none of the five audited ecosystem `.syhks00` tables (Ares / Kratos / Mix / Phobos and one closed-source platform) has a window in `0x533B00`-`0x533E00`. objdump from `0x533C3E`: `call 0x533D20` (5 bytes), next instruction `0x533C43` = address+5; the call is the hotkey-map rebuild inside the same engine registration routine and runs after the last `CommandClass::Array` append (the `call 0x538780` at `0x533C39`; no reference to the array follows this site), so the handler observes the completed list before the engine locks in `CommandClass::Hotkeys`; returning 0 lets the trampoline re-execute the call, so the hotkey rebuild still runs (HOOK-03); in a co-load, Ares's adjacent `0x533058` hook jumps back into the same routine (`0x533062`), so engine execution still reaches this site |
| 0x456B57 | 0x8 | Venture_UI_DrawRadialIndicator_LargeEllipse | Radial indicator: replace the outer-ring ellipse call with a double-precision outline scan when the engine's 32-bit ellipse rasterizer would overflow; replay the engine call below the safe radius | Rendering only; the second site 0x456BEB shares this handler (`DEFINE_HOOK_AGAIN`). Above the limit the stolen call is skipped, its nine stacked dwords (0x24) are unwound via the SyringeEx ESP modification and execution resumes at 0x456B5F; below it the handler returns 0 so the engine draws the ring unchanged |
| 0x456BEB | 0x7 | Venture_UI_DrawRadialIndicator_LargeEllipse | Radial indicator: same takeover for the inner ring; the handler reads the engine's local flag byte from the stack before the stolen window and restores it into EAX, resuming at 0x456BF2 | Rendering only; same handler as 0x456B57 (`DEFINE_HOOK_AGAIN`); `Game::DrawRadialIndicator` is the shared drawing routine behind selection rings, placement previews and superweapon targeting circles |
| 0x4694BB | 0x6 | Venture_TemporalAnySlot_Detonate | TemporalAnySlot: remember the weapon that delivered the Temporal warhead and create the techno's TemporalClass instance when the engine has none (the engine only creates it for a slot-0 Temporal warhead in TechnoClass::Init, then dereferences it here without a null check - the known crash at 0x71AF4D) | Feature; window `mov ecx,[ecx+0x274]` (6 B) ends at 0x4694C1 (`call 0x71AF20`).  ECX already holds the bullet's owner (loaded at 0x4694B4), EDI the warp target; returning 0 replays the stolen load with ECX untouched, so the engine's own Fire call picks up the instance created here.  The constructor registers itself in TemporalClass::Array and draws no random numbers, so every machine creates the same instance on the same (synchronised) detonation frame.  Free site - no audited ecosystem hook table, and the nearby Ares/Mix sites (0x469447, 0x469453, 0x469467) do not overlap |
| 0x71AB08 | 0x7 | Venture_TemporalAnySlot_WarpStep | TemporalAnySlot: rescale the per-frame warp step to the weapon that actually fired the warhead | Feature; window `pop edi`(1) + `pop esi`(1) + `pop ebp`(1) + `pop ebx`(1) + `add esp,0x24`(3) ends at 0x71AB0F (`ret`); ESI is the TemporalClass.  This is the shared tail of TemporalClass::Update: the vanilla continue path jumps here from 0x71A88F, and Ares' TemporalClass_UpdateA (its own hook at 0x71A84E) resumes at 0x71A88D, whose Phobos hook returns 0x71AB08 - so the handler runs under vanilla and under Ares + official Phobos alike.  The erase path also ends here; the handler skips itself when WarpRemaining is already non-positive.  Free site - none of the five audited ecosystem hook tables uses it |
| 0x6F42E3 | 0x7 | Venture_TechnoData_Init | Shared per-techno extension: allocate `TechnoDataExt` for every techno | Core; `mov dword [esi+0x49c],1` (7 B) near the end of `TechnoClass::Init`, next instruction 0x6F42EA; ESI holds the techno and all four concrete techno classes funnel through it.  Free site - none of the five audited ecosystem hook tables uses it.  The official Phobos alpha2 hooks the 0x6F42F7 tail with a 2-byte window, which the platform's hook verifier rejects (a near jump needs 5 bytes), hence this instruction; the only path that skips it is the early exit for an object whose `GetTechnoType()` is null |
| 0x6F4500 | 0x5 | Venture_TechnoData_DTOR | Shared per-techno extension: remove it in the `TechnoClass` base destructor | Core; window `push ecx`(1) + `push ebx`(1) + `push esi`(1) + `mov esi,ecx`(2) ends at 0x6F4505 (`xor ebx,ebx`), ECX holds the techno; all four concrete destructors chain into it.  Same address as the Ares / Kratos / Mix / Phobos-lineage `TechnoClass_DTOR` hooks (and one closed-source host removed from the public surface) - a framework point whose audited handlers return 0 |
| 0x6FF2AA | 0x6 | Venture_ROFCurve_Fire | ROFCurve: scale the reload delay the engine computed by the weapon's ROF curve | Feature; window `lea edx,[esi+0x2EC]` (6 B) ends at 0x6FF2B0; ESI = the firing techno, EAX = the delay `CalculateROF` (0x6FF289) produced, which the engine stores into `RearmTimer.TimeLeft` at 0x6FF2BB - rewriting EAX changes exactly that delay.  The weapon is taken from the function's own weapon-index argument (`[ebp+0xC]`, the value the engine passed to `GetWeapon` at 0x6FDD61); free site - none of the five audited ecosystem hook tables uses it |
| 0x6FE4B0 | 0x6 | Venture_ROFCurve_Fire | ROFCurve: the same handler on the `DiskLaser=yes` branch of `TechnoClass::Fire_At` | Feature; the same 6-byte `lea edx,[esi+0x2EC]` window, next instruction 0x6FE4B6.  On this branch EBX already holds the `DiskLaserClass*` created at 0x6FE483 (the fork read the weapon from EBX here, so its curve never applied to DiskLaser weapons), which is why the platform takes the weapon from the `Fire_At` weapon-index argument instead; declared with `DEFINE_HOOK_AGAIN` |
| 0x7729B0 | 0x5 | Venture_ROFCurve_WeaponType_INI | ROFCurve: read the seven `ROFCurve.*` keys of the weapon section the engine just read | Feature; window `mov al,1`(2) + `pop edi`(1) + `pop esi`(1) + `pop ebp`(1) ends at 0x7729B5 (`pop ebx`); ESI = `WeaponTypeClass*`, `[esp+0xE4]` = `CCINIClass*`.  Same address as the official Phobos / Ares / Kratos / Mix hooks (all size 5 and all returning 0, so a co-loaded platform appends to the chain); the handler only reads the INI and returns 0, which replays the pops |
| 0x7729C7 | 0x5 | Venture_ROFCurve_WeaponType_INI | ROFCurve: the same handler at the second success return path | Feature; the same window, next instruction 0x7729CC; declared with `DEFINE_HOOK_AGAIN`.  The failure path 0x7729D6 is deliberately not hooked (there is no section to read) |

Thirty-one hook records, twenty-seven distinct hook handler names
(`Venture_Example_UnitClass_DTOR` is installed at two addresses and
`Venture_UI_DrawRadialIndicator_LargeEllipse` at two addresses, both with
`DEFINE_HOOK_AGAIN`).

## What "size" means

A SyringeEx hook does not patch the whole function - it overwrites the first
`size` bytes of it with a jump and restores those bytes when the handler
returns.  The size therefore has to be chosen so that:

* the window contains **complete instructions only** (no instruction may be cut
  in half), and
* `address + size` is the start of the **next** instruction.

If two hooks share the same address, SyringeEx uses the largest declared size
for the window (`max(size)`).  Every size in this catalogue comes from the
instruction listing at the hook site; when the site is shared with the wider
Phobos / YRpp ecosystem the declared size also matches the convention used there.
Do not invent a new size when adding a hook to an existing site - derive it from
the instruction listing and check it against the ecosystem conventions.

## Adding a hook

1. Derive the address and verify the instruction window as described above.
2. Add `DEFINE_HOOK` / `DEFINE_HOOK_AGAIN` in the appropriate source file
   (`src/Engine/Hooks.Lifecycle.cpp` for base lifecycle hooks, the feature
   module for feature-specific hooks).
3. Add one row to the table above with address, size, handler name and a short
   purpose.  The parser matches address, size and name exactly, so a typo in
   either place is a verification failure.
4. Add feature-specific hooks only in feature modules; keep the base hook
   surface small so derived platforms can replace modules without dragging
   unrelated patches along.
5. Rebuild, then run `python3 scripts/verify_build.py`.  The table is checked
   against the DLL's `.syhks00` records, and with the engine executable supplied
   the windows themselves are validated against `gamemd.exe`'s instructions
   (offline, read-only - the engine is never executed):

   ```bash
   VCS_GAMEMD_EXE=/path/to/gamemd.exe python3 scripts/verify_build.py
   # or, standalone:
   python3 scripts/verify_hook_windows.py --gamemd /path/to/gamemd.exe
   ```

   Without an engine path the verify script reports that one check as `[SKIP]`
   with a warning (a skip, never a silent pass); the rest of the checks still
   run.

## Hook sites not used by the base

The following sites are **not** in the table above and **not** in the DLL.  If one
is implemented it moves into the table.

* Host-specific feature hooks for Ares / Phobos / Kratos environments (the base
  only detects hosts and exposes the set; it installs no host-specific hook).

## See also

* `docs/EngineBindings.md` - the binding layers this catalogue belongs to.
* `docs/MakeYourOwnPlatform.md` - how a derived platform adds its own features
  (and hooks) without touching the base.
* `scripts/verify_build.py` - the checker that enforces this table.
* `scripts/verify_hook_windows.py` - the offline, read-only validator that decodes
  `gamemd.exe` and proves every window is a complete-instruction patch site.
