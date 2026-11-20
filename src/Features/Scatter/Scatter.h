#pragma once

#include "Features/Scatter/ScatterMath.h"

// ---------------------------------------------------------------------------
// Scatter core feature module (P1 / R10) - internal surface.
//
// The module implements the platform's own `LW.BallisticScatter.*` scatter
// system (design: the workspace's `VentureLW弹道与散布体系设计.md`, a document
// outside this repository).  The whole geometry lives in ScatterMath.h, which is
// engine-free and unit-tested; this header is the thin surface between the
// engine hooks (Hooks.Scatter.cpp) and the engine glue (ScatterFeature.cpp).
//
// State of the round (see the handover manual in the workspace):
//
//   R10-1  read-only host-sample probe (done, verified on the real machine).
//   R10-2  the `LW.BallisticScatter.*` key surface, the radius remap on both
//          Fire_At branches, the true ellipse at the Fire_At exit and the radius
//          shaping of the classic MoveTo path (done, verified on the real
//          machine).
//   R10-3  burst-point scatter for guided missiles (0x468DCF, ShapeBurstOffset);
//          superseded for ROT > 0 by R10-4, kept as the fallback for an
//          `Arm > 0` fuse-only projectile that has no guidance to move.
//   R10-4  this commit: the physical flight offset for ROT > 0 missiles
//          (0x4688A9 launch draw + 0x466BAF aim shift + 0x4677D3 self
//          detonation + 0x4665E9 cleanup).  Automatic when the projectile uses
//          the platform's scatter keys - no separate opt-in (user decision
//          2026-11-20: the fork's `...Physical=yes` split is not ported).
//   R10-5  documentation, guides and the closing real-machine test.
//
// There is no conflict detection and no separate enable switch beyond
// `[Features] Scatter`: a projectile that writes any `LW.BallisticScatter.*` key
// is shaped, a projectile that writes none keeps the host behaviour (the
// platform deliberately re-derives the engine's draw instead of touching the
// vanilla keys, so a co-loaded Ares / official Phobos reads its own keys
// untouched).
// ---------------------------------------------------------------------------

class BulletClass;
class BulletTypeClass;
class CCINIClass;

namespace venture::features::scatter
{
// The `[Features] Scatter` switch (default on).  When the module is off every
// hook returns immediately and no key is read.
bool IsEnabled();

// The configuration of one projectile type, or null when the type writes no
// `LW.BallisticScatter.*` key (or the module is off).  Valid from the moment
// rules(md).ini is parsed; the map is never modified during a battle.
const ScatterConfig* FindConfig(const BulletTypeClass* pType);

// Resolved `[General] LW.Divergence` (default kGlobalDivergenceDefault).  The
// per-projectile key wins; this is only the fallback value.
double GlobalDivergence();

// Reads this projectile's `LW.BallisticScatter.*` keys from the section the
// engine has just parsed.  Called from the BulletTypeClass::LoadFromINI success
// epilogue (0x46C41C) by Hooks.Scatter.cpp; exposed here so the hook file has
// no INI logic of its own.
void LoadScatterConfigFromIni(BulletTypeClass* pType, CCINIClass* pINI);

// --- physical flight offset (ROT > 0 missiles) -----------------------------
//
// A ROT > 0 projectile that writes both LW.BallisticScatter.Min and .Max gets a
// physical scatter: the offset is drawn once at launch (BulletClass::MoveTo's
// merge point, 0x4688A9), the guidance aim point is moved by it on every frame
// (0x466BAF) and the missile is set off at that point (0x4677D3).  The state is
// per bullet, transient and non-savegame: a scenario clear drops the whole table
// and the affected missiles fall back to the engine's own guidance.
struct PhysicalScatterState
{
	int OffsetX = 0; // world-space offset added to the live target's aim point
	int OffsetY = 0;
	int AimX = 0; // the point the missile is steering at (updated every frame)
	int AimY = 0;
	int AimZ = 0;
	int ShapedRadius = 0; // diagnostics: radius before the distance ramp
	int Radius = 0;       // diagnostics: the polar radius actually used
};

// Mutable state of one bullet, or null when the bullet is not physically
// scattered.  A one-entry cache makes the repeated fetch of the same bullet in
// one update (aim then detonation) cheap.
PhysicalScatterState* FetchPhysicalScatter(const BulletClass* pBullet);

// Registers the state of a bullet.  Returns false when the bullet already has
// one (the launch site is idempotent) or the module is off.
bool BeginPhysicalScatter(const BulletClass* pBullet, const PhysicalScatterState& state);

// Drops the state (bullet destructor).
void EndPhysicalScatter(const BulletClass* pBullet);

// Drops the whole table (scenario clear).
void ClearPhysicalScatter();
} // namespace venture::features::scatter
