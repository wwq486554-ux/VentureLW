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
//   R10-2  this commit: the `LW.BallisticScatter.*` key surface, the radius
//          remap on both Fire_At branches, the true ellipse at the Fire_At exit
//          and the radius shaping of the classic MoveTo path.
//   R10-3  burst-point scatter for engine-guided ROT/Arm missiles (0x468DCF).
//   R10-4  the physical flight offset (B16, BulletDataExt state).
//   R10-5  documentation, guides and the closing real-machine test.
//
// There is no conflict detection and no separate enable switch beyond
// `[Features] Scatter`: a projectile that writes any `LW.BallisticScatter.*` key
// is shaped, a projectile that writes none keeps the host behaviour (the
// platform deliberately re-derives the engine's draw instead of touching the
// vanilla keys, so a co-loaded Ares / official Phobos reads its own keys
// untouched).
// ---------------------------------------------------------------------------

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
} // namespace venture::features::scatter
