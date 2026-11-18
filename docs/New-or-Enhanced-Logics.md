# New or Enhanced Logics

## ROF curve (weapon-level spin-up)

- A weapon can fire faster - or slower - the longer it keeps firing.  This is
  the vanilla gattling idea moved from the unit onto the weapon: it is the same
  weapon from the first shot to the last, nothing is swapped and no staged
  weapon list is involved, so any weapon type can carry its own curve.
- Only the delay between two shots changes.  The projectile, the firing
  animation, the report, the warhead and the damage are untouched.
- The curve runs from `ROFCurve.Start` (progress `0`) to `ROFCurve.End`
  (progress `1`) over `ROFCurve.Ramp` frames.  Neither end has to be the slow
  one: `Start` is what the weapon fires at when the chain begins and `End` what
  it reaches at the top of the curve, so a weapon may just as well fire fastest
  first and wind down.  `ROFCurve.Power` shapes how it gets there: `1.0` is a
  straight line, above `1.0` stays near `Start` and then moves quickly, below
  `1.0` leaves `Start` immediately.
- `ROFCurve.Decay` brings the curve back down once the weapon stops firing: after
  that many frames without a shot the progress has fallen back to the start.
  `ROFCurve.Decay=0` resets it at once.
- `ROFCurve.Cycle` decides what reaching the top means: `no` holds the weapon at
  `End`, `yes` restarts the curve from `Start`.
- The curve is applied as a ratio to the reload delay the engine had already
  computed, so everything else that shapes the firing rate - the AttachEffect ROF
  multiplier, veterancy and `ROF.RandomDelay` - keeps working.
- Only the interval between two reloads is affected, so on a `Burst` weapon this
  is the interval between bursts, not the delay inside a burst.
- The spin-up state is tracked **per weapon**: a techno that carries several
  weapons keeps one curve for each of them, and firing one of them never touches
  another one's progress.  This is what a unit whose ammo logic falls back to
  another weapon needs - the fallback weapon's curve keeps building across the
  other weapon's shots instead of starting over, and a pause is settled by that
  weapon's own `ROFCurve.Decay`.  The chains are not saved: loading a save starts
  each curve over, which is what it looks like anyway.
- The module is opt-in per weapon: a weapon that does not write `ROFCurve=yes`
  and `ROFCurve.End` keeps its normal rate of fire, owns no chain and is not
  touched by the feature at all.  `[Features] ROFCurve` (default on) switches the
  module itself.

In `rulesmd.ini`:
```ini
[SOMEWEAPON]                 ; WeaponType
ROFCurve=no                  ; boolean
ROFCurve.Start=              ; integer, frames, default to the weapon's own ROF
ROFCurve.End=                ; integer, frames, required
ROFCurve.Ramp=30             ; integer, frames
ROFCurve.Power=1.0           ; floating point
ROFCurve.Decay=0             ; integer, frames
ROFCurve.Cycle=no            ; boolean
```

```{note}
- `ROFCurve.End` has to be set; without it the weapon is treated as not having a
  curve and keeps its normal rate of fire.
- `ROFCurve.Power` is clamped to `[0.1, 10.0]`, and a non-positive `Ramp` falls
  back to 30 frames; both corrections are reported in the platform log.
- A shot whose reload delay the engine computed as less than one frame is left
  alone rather than scaled.
- With `ROFCurve.Cycle=yes` the curve restarts as soon as it reaches the top, so
  the weapon drops back to the other end for one shot.
- `yes`/`no` (or `true`/`false`, `1`/`0`) are the accepted boolean spellings for
  the rules INI; `on`/`off` are read as "not written" by the engine's INI parser.
- A very large `End` combined with a large engine-computed delay saturates at the
  largest frame count instead of overflowing.
- The chain is kept per techno and per weapon type, never in the savegame, and
  the curve only reads the rules INI at load time - multiplayer sync is not
  affected by it.
```

### Implementation notes (platform)

- Injection points: the two `TechnoClass::Fire_At` reload sites - the common
  branch (`0x6FF2AA`, size 6) and the `DiskLaser=yes` branch (`0x6FE4B0`,
  size 6) - and the two `WeaponTypeClass::LoadFromINI` success epilogues
  (`0x7729B0` / `0x7729C7`, size 5, the same sites the official Phobos, Ares,
  Kratos and Mix use; the platform appends and returns 0).  See
  `docs/HookCatalogue.md`.
- The seven keys are read once, when the engine has parsed the weapon's section,
  and resolved into a configuration table keyed by the weapon type.  The table is
  feature-local on purpose: weapon types are process-lifetime objects, while the
  platform's extension stores are cleared on scenario clear (the savegame load
  path runs that clear too), so a type extension store would lose the curve
  configuration after every load.  Per-techno chains live in the shared
  `TechnoDataExt` (`src/Ext/TechnoDataExt.h`).
- The `DiskLaser=yes` branch is handled correctly: on that branch the register
  the fork used holds the `DiskLaserClass*` it just created, so the platform
  reads the weapon from the `Fire_At` weapon-index argument instead.
- Not migrated in this batch (they belong to features that do not exist in the
  platform yet): `SweepFire.Cooldown` scaling and `Temporal.Interval` scaling.
  The per-chain `LastRearm` field is already the interface they will use.

> Ported from Phobos-LWModified (commits `47023891` / `b950ac50` / `6c54bd6f`,
> 2026-10).  The seven keys and the curve are unchanged; the injection points and
> the state storage follow the platform's rules.
