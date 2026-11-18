# Fixed or Improved Logics

## Vanilla fixes

### Large radial indicator drawing

- The radial indicator drawn for a selected building, a building placement
  preview, a superweapon targeting circle and any other caller of the engine's
  `Game::DrawRadialIndicator` routine used to vanish completely once its radius
  got large - around a 30 cell range and beyond nothing was drawn at all,
  instead of a partially broken ring.
- The cause is the vanilla ellipse rasterizer: it works on 32-bit signed
  integer discriminants whose leading term grows as the cube of the pixel radius
  (the indicator draws two concentric ellipses for its 2 pixel ring), so the
  comparisons overflow and every point gets rejected.
- VentureLW now watches both ellipse calls of the indicator routine.  While the
  radius stays safely below the overflow point the engine draws the ring exactly
  as before; beyond it the platform draws the outline itself with double
  precision scan lines, whose cost depends on the visible area rather than on
  the radius.  Large ranges get a complete, correctly shaped ring again.
- Nothing else about the indicator changes: the range and colour selection, the
  visibility rules and the rotating short lines are untouched.  This is
  rendering only - no game state is written, so multiplayer sync is unaffected.
- No new INI keys.

> Ported from Phobos-LWModified (commits `41bc10fe` / `1865743c`, 2026-10-24).

### Temporal warheads from any weapon slot

- The engine creates a techno's Temporal instance (`TechnoClass::TemporalImUsing`)
  once, in `TechnoClass::Init`, and only when the warhead in **weapon slot 0** is
  `Temporal=yes`.  When a Temporal warhead arrives from any other weapon - another
  slot, an `AirburstWeapon`, a `ShrapnelWeapon`, a `ForceWeapon`, an Ares
  superweapon - the detonation path at `0x4694BB` dereferences that missing
  instance without a null check and the game crashes at `0x71AF4D` (a crash the
  community already documents as a known vanilla one).
- VentureLW now creates the instance on the first Temporal detonation
  (`0x4694BB`, size 6, right before the engine's `TemporalClass::Fire` call).  One
  hook covers every delivery path; the stolen load is replayed with the owner
  register untouched, so the engine's own call picks the new instance up.  A
  techno whose slot 0 is already Temporal (the vanilla Chrono Legionnaire / Chrono
  Tank) has the instance from init and is not touched at all.
- A second vanilla defect becomes visible once the crash is gone: the erase rate
  of a running warp is derived from the techno's **current** weapon selection, not
  from the weapon that delivered the warhead.  After a forced-slot / any-slot shot
  the selection has already been released, so a 1000-damage Temporal weapon erases
  at a 20-damage weapon's speed (105 frames instead of 3 in the recorded case).
  VentureLW remembers the weapon that delivered the warhead (transient per-techno
  state, never saved) and, on the shared tail of `TemporalClass::Update`
  (`0x71AB08`, size 7 - reached by the vanilla continue path and by Ares'
  `TemporalClass_UpdateA` alike), rescales the frame's engine step to that weapon's
  `Damage`.  The step the engine computed is reused, so the multipliers of a
  co-loaded official Phobos (`Temporal.ApplyMultiplier` and friends) keep their
  effect; only the `Damage` is corrected.
- The correction is skipped when the engine-selected weapon already carries a
  Temporal warhead, so plain Chrono Legionnaire / Chrono Tank behaviour and Ares'
  `WeaponIndex_Warp` usage are unchanged.  Known boundary: the correction is
  proportional, so warhead-level factors of the engine-selected weapon (rather
  than of the fired one) keep their own contribution - unlike the fork, which
  replaces weapon and warhead together.  The erase rate still follows the fired
  weapon's `Damage`, which is what this fix is about.
- Deterministic: detonation is part of the synchronised simulation and the
  Temporal constructor draws no random numbers, so every machine creates the same
  instance on the same frame and computes the same rate.  Multiplayer sync is
  unaffected.
- No new INI keys; the whole module is the `[Features] TemporalAnySlot` switch
  (default on).

> Ported from Phobos-LWModified (commit `c2341b52`, 2026-09-22).  The injection
> points are different: the fork reimplements the engine's
> `TemporalClass::GetWarpPerStep` because it owns that function, while the
> platform leaves `0x71AB10` to a co-loaded official Phobos and corrects the
> result at the update tail both hosts share.

### Armed engineers

- Vanilla hardcodes `if (Type->Engineer && action == Action::Attack) return
  Action::NoMove;` at `0x51E6B4`, so an `Engineer=yes` infantry can never get an
  attack cursor on a unit even when INI gives it a real weapon.  Force-firing a
  building is worse: deeper in the same routine every non-allied capturable
  building is rewritten back to `Action::Capture` (`0x51EE91` -> `0x51EEEF`), which
  no jump inside the function can dodge.
- VentureLW now does two things.  It releases the attack clamp at `0x51E6C4` for
  engineers that really carry an attack weapon (the weapon the engine would use
  against the target exists, has a warhead and is not a bomb-disarm tool), so an
  armed engineer can right-click and Ctrl-attack enemy units like any other armed
  infantry.  It also takes over the `InfantryClass::MouseOverObject` vtable slot
  (`0x7EB0CC`, vanilla `0x51E3B0`) at the outermost layer and rewrites a Ctrl
  force-fired building result to `Action::Attack`; the previous implementation is
  called first, so a co-loaded host wrapper keeps working.
- Behaviour: with Ctrl a building is attacked; without Ctrl a building click keeps
  the vanilla capture / repair / enter actions; Ivan-bomb disarm keeps its own
  cursor; an engineer without an attack weapon (or with only a bomb-disarm
  weapon) behaves exactly as before; non-engineer infantry is not touched at all.
- Known trade-offs (carried over from the fork): Ctrl means attack for *any*
  building, allied ones included, and the AI attack scripts still convert
  engineers to capture missions - this feature is about manual attacks.  Guard
  auto-targeting stays disabled for engineers like in vanilla.
- No new INI keys; the whole module is `[Features] ArmedEngineers` (default on).

> Ported from Phobos-LWModified (commit `c2341b52`, 2026-09-26).  The fork patched
> the vtable slot statically; the platform reads the slot after all host DLLs
> patched, chains to the previous implementation and writes it under
> `VirtualProtect` (the slot is in read-only `.rdata`) - see
> `docs/EngineBindings.md`.
