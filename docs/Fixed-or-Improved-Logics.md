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
