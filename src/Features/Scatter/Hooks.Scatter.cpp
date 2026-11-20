// ---------------------------------------------------------------------------
// Scatter core feature module (P1 / R10) - the engine hooks.
//
// The module shapes the scatter the host has already produced, and displaces
// engine-guided projectiles itself (the only two entries that draw random
// numbers: at launch for a guided missile, on the detonation frame for a
// fuse-only projectile; see ScatterMath.h).  The sites and their windows were
// audited instruction by instruction against the supported gamemd build (the
// round's engine-site record in the workspace) and against the Ares / Kratos /
// official Phobos / Mix hook tables:
//
//   TechnoClass::Fire_At scatter block (0x6FE67D .. 0x6FE8EE)
//     0x6FE67D / 6 - the block decided to run (je for !Inaccurate): capture the
//                     projectile type, the unscattered aim base
//                     ([esp+0x30..0x38]), the 3-D distance, the firing weapon
//                     range and the engine branch (visible / legacy).
//     0x6FE71C / 6 - visible branch: EAX is the host's radius draw.  Remap it
//                     onto LW.BallisticScatter.Min/Max/Sigma and overwrite EAX;
//                     the replayed `mov ebx,eax` then carries the shaped radius.
//                     Ares hooks 0x6FE709 and jumps *to* this address with its
//                     own draw in EAX, so this handler is the post-Ares entry
//                     and must never draw or remap a second time.
//     0x6FE73F / 6 - visible branch: EAX is the engine's own
//                     draw * distance / GetWeaponRange; the handler discards it
//                     and recomputes `shaped * (distance / range) ^ (1/Div)`
//                     with the firing weapon's own Range (TechClass::
//                     GetWeaponRange can return the minimum of attached turret
//                     weapons, which silently broke the fork's ramp - measured
//                     in the fork's real-machine test).
//     0x6FE821 / 6 - legacy Inaccurate branch: EAX is the host's radius draw.
//                     Remap it, then add the distance ramp the engine never had
//                     on this branch (the fork's P0-2, kept).
//     0x6FE8EE / 6 - the block's common exit: the scattered offset is final in
//                     crdOffset ([esp+0x94..0x9C]) and the facing / velocity /
//                     firing solution are derived from it right afterwards, so
//                     this is the only place a 2-D ellipse can be applied and
//                     still agree with everything downstream.
//
//   BulletClass::MoveTo classic path (0x4687F8)
//     0x4687F8 / 6 - FlakScatter + Inviso radius.  EAX already contains
//                     `draw * distance / range`; official Phobos / Mix / the
//                     fork also hook this site (all return 0, all rewrite EAX),
//                     and this DLL is last in the chain, so the handler sees the
//                     host's final radius.  Only the radius is shaped here: the
//                     post-scatter sites of this path are owned by Ares
//                     (0x4688BD always returns 0x468A3F) and by the official
//                     Phobos / Mix obstacle handler (0x4688A9 returns
//                     0x4688BD for Inviso bullets), so a chained handler there
//                     would never run - the ellipse limitation is documented.
//
//   0x46C41C / 0xA - BulletTypeClass::LoadFromINI success epilogue: reads the
//                     projectile's `LW.BallisticScatter.*` keys into the
//                     feature-local type table.  Ares / Kratos / official
//                     Phobos / Mix hook the same site and all return 0.
//
//   Engine-guided projectiles (the platform's own displacement, B16 / B14)
//     0x4688A9 / 6 - BulletClass::MoveTo merge point: for a ROT > 0 missile that
//                     writes both bounds, draw the physical scatter offset once
//                     and register the per-bullet state (see the block comment
//                     above the handler).
//     0x466BAF / 6 - per frame in the guidance block: move the engine's aim
//                     point by the offset and let the proximity fuse follow it.
//     0x4677D3 / 5 - per frame at the head of the detonation chain: set the
//                     missile off where it is steering (segment test).
//     0x4665E9 / 0xA - BulletClass destructor: drop the per-bullet state.
//     0x468DCF / 6 - the Inaccurate branch's `jne 0x468FF4`, i.e. the jump that
//                     skips every target snap.  This is the burst-point fallback
//                     for a fuse-only projectile (`Arm > 0`, no `ROT`), which
//                     has no guidance to move: the handler rewrites
//                     [esp+0xC..0x14] on the detonation frame and returns 0, so
//                     the replayed jump carries the new point into the Airburst /
//                     Cluster / Detonate paths.  The fork's 0x468FF4 site cannot
//                     be used: the official Phobos / Mix / fork handlers there
//                     always return non-zero and this DLL is last in the chain,
//                     so a handler there never runs.
//
// Every handler returns 0, i.e. the trampoline replays the overridden
// instruction(s).  The handlers only touch behaviour when the shooting
// projectile's own section carries `LW.BallisticScatter.*` keys; otherwise they
// leave every register and the offset exactly as the host produced them.
// ---------------------------------------------------------------------------

#include <Syringe.h>
#include <SpecificStructures.h>

#include <Helpers/Macro.h>

#include <cmath>
#include <unordered_map>

#include "Core/Log.h"
#include "Engine/EngineCompat.h"
#include "Features/Scatter/Scatter.h"

using venture::features::scatter::ApplyFireAtDistance;
using venture::features::scatter::ApplyScatterAspect;
using venture::features::scatter::BeginPhysicalScatter;
using venture::features::scatter::BurstOffset;
using venture::features::scatter::ClampScatterAim;
using venture::features::scatter::EndPhysicalScatter;
using venture::features::scatter::FetchPhysicalScatter;
using venture::features::scatter::FindConfig;
using venture::features::scatter::HostBounds;
using venture::features::scatter::IsEnabled;
using venture::features::scatter::LoadScatterConfigFromIni;
using venture::features::scatter::NormalizeDraw;
using venture::features::scatter::NormalizeScatterAspect;
using venture::features::scatter::kLeptonsPerCell;
using venture::features::scatter::NormalizeScatterChance;
using venture::features::scatter::PhysicalScatterState;
using venture::features::scatter::ScatterAimBounds;
using venture::features::scatter::ScatterConfig;
using venture::features::scatter::ScatterPath;
using venture::features::scatter::ShapeBurstOffset;
using venture::features::scatter::ShapeClassicRadius;
using venture::features::scatter::ShapeFireAtDraw;
using venture::features::scatter::ShapedRadius;
using venture::Log;

namespace
{
// Diagnostics: bounded number of per-shot debug lines, so an in-game check can
// see exactly what the shaping did without flooding a long battle's log.  The
// cap is per projectile type (a test that fires one configured projectile type
// first must not silence the other paths) plus one global safety cap.
constexpr int kShotLogPerTypeLimit = 20;
constexpr int kShotLogTotalLimit = 200;
constexpr int kUnconfiguredNoteLimit = 16;
int g_shotLogTotal = 0;
std::unordered_map<const BulletTypeClass*, int> g_shotLogCounts;
std::unordered_map<const BulletTypeClass*, int> g_notedTypes;

// One Fire_At call is a single synchronous pass through the scatter block and
// the game runs the logic on one thread, so a file-static capture is enough.
struct FireAtCapture
{
	const BulletTypeClass* Type = nullptr;
	const ScatterConfig* Config = nullptr; // null = the shaping does not apply
	ScatterPath Path = ScatterPath::FireAtVisible;
	int Global = 0;
	int BaseX = 0;
	int BaseY = 0;
	int BaseZ = 0;
	double Distance = 0.0;
	int Range = 0; // the firing weapon's Range, 0 when it could not be resolved
	int RawDraw = -1;
	int ShapedRadius = 0;
	int FinalRadius = 0;
	bool ChanceMiss = false;
};

FireAtCapture g_fireAt;

void ClearCapture()
{
	g_fireAt.Type = nullptr;
	g_fireAt.Config = nullptr;
	g_fireAt.ChanceMiss = false;
	g_fireAt.RawDraw = -1;
	g_fireAt.ShapedRadius = 0;
	g_fireAt.FinalRadius = 0;
}

const char* PathName(const ScatterPath path)
{
	switch (path)
	{
	case ScatterPath::FireAtVisible:
		return "fire_at_visible";
	case ScatterPath::FireAtLegacy:
		return "fire_at_legacy";
	case ScatterPath::Classic:
		return "classic_inviso";
	}

	return "unknown";
}

// The engine resolves the weapon from its own Fire_At argument; the same index
// gives the firing weapon's Range, which is what the distance ramp has to be
// normalized against (not TechnoClass::GetWeaponRange, see the file comment).
int ResolveFiringWeaponRange(TechnoClass* const pFirer, const int weaponIndex)
{
	if (!pFirer || weaponIndex < 0)
		return 0;

	const WeaponStruct* const pSlot = pFirer->GetWeapon(weaponIndex);
	return pSlot && pSlot->WeaponType ? pSlot->WeaponType->Range : 0;
}

// True while this projectile type still has shot-log budget (and the session is
// under the global safety cap).  Logs the per-type cap line exactly once.
bool ShouldLogShot(const BulletTypeClass* const pType)
{
	if (!pType)
		return false;

	int& count = g_shotLogCounts[pType];

	if (count >= kShotLogPerTypeLimit)
	{
		if (count == kShotLogPerTypeLimit)
		{
			++count;
			Log::Info("[Scatter] shaped-shot debug lines for %s capped at %d", pType->ID,
				kShotLogPerTypeLimit);
		}

		return false;
	}

	if (g_shotLogTotal >= kShotLogTotalLimit)
	{
		if (g_shotLogTotal == kShotLogTotalLimit)
		{
			++g_shotLogTotal;
			Log::Info("[Scatter] shaped-shot debug lines capped at %d for this session",
				kShotLogTotalLimit);
		}

		return false;
	}

	++count;
	++g_shotLogTotal;
	return true;
}

// One bounded debug line per shaped shot, printed at the scatter block's exit
// where the final offset is known.  Only called for shots whose projectile type
// carries LW.* keys, so an unconfigured battle never fills the log.
void LogFireAtShot(const int beforeX, const int beforeY, const int afterX, const int afterY,
	const bool ellipseApplied)
{
	if (!ShouldLogShot(g_fireAt.Type))
		return;

	const ScatterConfig& config = *g_fireAt.Config;
	const double u = NormalizeDraw(g_fireAt.RawDraw, HostBounds(g_fireAt.Path, g_fireAt.Global));

	if (g_fireAt.ChanceMiss)
	{
		Log::Debug("[Scatter] shot path=%s type=%s raw=%d global=%d u=%.3f chance=%.2f -> no scatter "
				   "(chance miss)",
			PathName(g_fireAt.Path), g_fireAt.Type->ID, g_fireAt.RawDraw, g_fireAt.Global, u, config.Chance);
		return;
	}

	Log::Debug("[Scatter] shot path=%s type=%s raw=%d global=%d u=%.3f shaped=%d final=%d dist=%.0f "
			   "range=%d divergence=%.2f aspect=%.2f offset=(%d,%d) ellipse=%d -> (%d,%d)",
		PathName(g_fireAt.Path), g_fireAt.Type->ID, g_fireAt.RawDraw, g_fireAt.Global, u,
		g_fireAt.ShapedRadius, g_fireAt.FinalRadius, g_fireAt.Distance, g_fireAt.Range,
		config.Divergence, NormalizeScatterAspect(config.Aspect), beforeX, beforeY, ellipseApplied ? 1 : 0,
		afterX, afterY);
}

// Logs one projectile type that is fired with the scatter block but has no
// LW.* keys, once per type: the in-game check needs to distinguish "shaped"
// from "left to the host".
void NoteUnconfiguredType(const BulletTypeClass* const pType)
{
	if (!pType || g_notedTypes.count(pType) || g_notedTypes.size() >= kUnconfiguredNoteLimit)
		return;

	g_notedTypes[pType] = 1;
	Log::Debug("[Scatter] %s: no LW.BallisticScatter.* keys; the host scatter is used unchanged "
			   "(logged once per projectile type)",
		pType->ID);
}

// One bounded debug line for the classic path, which has no Fire_At exit of its
// own.
void LogClassicShot(const BulletTypeClass* const pType, const int engineRadius, const double u,
	const ShapedRadius& shaped, const double distance, const int range)
{
	if (!ShouldLogShot(pType))
		return;

	Log::Debug("[Scatter] shot path=%s type=%s engine_radius=%d u=%.3f shaped=%d dist=%.0f range=%d "
			   "-> %s",
		PathName(ScatterPath::Classic), pType->ID, engineRadius, u, shaped.Radius, distance, range,
		shaped.Scatter ? "shaped" : "no scatter (chance miss)");
}

// One bounded debug line for a burst point (B14/6A): the displaced point, the
// sample that produced it, or why the engine's own point was kept.  The cap is
// the shared per-projectile-type one of the shot lines.
void LogBurstShot(const BulletTypeClass* const pType, const double u, const double distance,
	const int range, const ScatterConfig& config, const BurstOffset& burst, const int afterX,
	const int afterY)
{
	if (!ShouldLogShot(pType))
		return;

	const double chance = NormalizeScatterChance(config.Chance);

	if (burst.ChanceMiss)
	{
		Log::Debug("[Scatter] burst type=%s u=%.3f chance=%.2f dist=%.0f range=%d -> no scatter "
				   "(chance miss)",
			pType->ID, u, chance, distance, range);
		return;
	}

	if (!burst.Scatter)
	{
		Log::Debug("[Scatter] burst type=%s u=%.3f chance=%.2f dist=%.0f range=%d -> no scatter "
				   "(zero radius)",
			pType->ID, u, chance, distance, range);
		return;
	}

	Log::Debug("[Scatter] burst type=%s u=%.3f chance=%.2f shaped=%d final=%d dist=%.0f range=%d "
			   "divergence=%.2f aspect=%.2f offset=(%d,%d) at=(%d,%d)",
		pType->ID, u, chance, burst.ShapedRadius, burst.Radius, distance, range, config.Divergence,
		NormalizeScatterAspect(config.Aspect), burst.OffsetX, burst.OffsetY, afterX, afterY);
}

// One bounded debug line per physical-scatter launch (B16): the sample, the aim
// point the missile is now steering at, and whether the map clamp moved it.
void LogPhysicalLaunch(const BulletTypeClass* const pType, const double u, const double distance,
	const int range, const ScatterConfig& config, const BurstOffset& offset, const CoordStruct& base,
	const CoordStruct& aim, const bool clamped)
{
	if (!ShouldLogShot(pType))
		return;

	const double chance = NormalizeScatterChance(config.Chance);

	if (offset.ChanceMiss)
	{
		Log::Debug("[Scatter] physical type=%s u=%.3f chance=%.2f dist=%.0f range=%d -> no scatter "
				   "(chance miss)",
			pType->ID, u, chance, distance, range);
		return;
	}

	if (!offset.Scatter)
	{
		Log::Debug("[Scatter] physical type=%s u=%.3f chance=%.2f dist=%.0f range=%d -> no scatter "
				   "(zero radius)",
			pType->ID, u, chance, distance, range);
		return;
	}

	Log::Debug("[Scatter] physical type=%s u=%.3f chance=%.2f shaped=%d final=%d dist=%.0f range=%d "
			   "divergence=%.2f aspect=%.2f offset=(%d,%d) at=(%d,%d) aim=(%d,%d,%d) clamped=%d",
		pType->ID, u, chance, offset.ShapedRadius, offset.Radius, distance, range, config.Divergence,
		NormalizeScatterAspect(config.Aspect), aim.X - base.X, aim.Y - base.Y, base.X, base.Y, aim.X,
		aim.Y, aim.Z, clamped ? 1 : 0);
}

// One bounded debug line when a physical-scatter missile sets itself off, so the
// in-game check can see that the burst really happened at the scattered point.
void LogPhysicalDetonate(const BulletTypeClass* const pType, const CoordStruct& at,
	const CoordStruct& aim, const double distance)
{
	if (!ShouldLogShot(pType))
		return;

	Log::Debug("[Scatter] physical detonate type=%s at=(%d,%d,%d) aim=(%d,%d,%d) dist=%.0f",
		pType->ID, at.X, at.Y, at.Z, aim.X, aim.Y, aim.Z, distance);
}
} // namespace

// TechnoClass::Fire_At, the engine's scatter block decided to run
// (`je 0x6FE8EE` when !Inaccurate; the Arcing check follows).  ECX is the
// BulletTypeClass, ESI the firing TechnoClass, the unscattered aim offset
// (muzzle -> aim) is at [esp+0x30..0x38] and is copied into crdOffset right
// before this point; the weapon index is the function's own second argument
// ([ebp+0xC]).
DEFINE_HOOK(0x6FE67D, Venture_Scatter_Capture, 0x6)
{
	ClearCapture();

	if (!IsEnabled())
		return 0;

	GET(BulletTypeClass*, pType, ECX);

	// Both flags are the engine's own gate: only Inaccurate + Arcing reaches the
	// two Fire_At branches this module shapes.
	if (!pType || !pType->Inaccurate || !pType->Arcing)
		return 0;

	const ScatterConfig* const pConfig = FindConfig(pType);

	if (!pConfig)
	{
		NoteUnconfiguredType(pType);
		return 0;
	}

	const int global = RulesClass::Instance->BallisticScatter;

	if (global <= 0)
		return 0;

	GET(TechnoClass*, pFirer, ESI);
	GET_BASE(int, weaponIndex, 0xC);

	g_fireAt.Type = pType;
	g_fireAt.Config = pConfig;
	g_fireAt.Path = (pType->FlakScatter && !pType->Inviso) ? ScatterPath::FireAtVisible
														  : ScatterPath::FireAtLegacy;
	g_fireAt.Global = global;
	g_fireAt.BaseX = R->Stack<int>(0x30);
	g_fireAt.BaseY = R->Stack<int>(0x34);
	g_fireAt.BaseZ = R->Stack<int>(0x38);

	const double dx = static_cast<double>(g_fireAt.BaseX);
	const double dy = static_cast<double>(g_fireAt.BaseY);
	const double dz = static_cast<double>(g_fireAt.BaseZ);
	g_fireAt.Distance = std::sqrt(dx * dx + dy * dy + dz * dz);
	g_fireAt.Range = ResolveFiringWeaponRange(pFirer, weaponIndex);

	return 0;
}

// Visible branch: the host's radius draw has just returned in EAX (Ares, when
// loaded, replaced the engine's own draw at 0x6FE709 and returns to this very
// address).  The overridden window is `fld [esp+0x18]` (4 B) + `mov ebx,eax`
// (2 B); the trampoline replays both, so the shaped radius in EAX becomes EBX
// and the engine's own distance multiplication uses it.
DEFINE_HOOK(0x6FE71C, Venture_Scatter_VisibleDraw, 0x6)
{
	if (!g_fireAt.Config || g_fireAt.Path != ScatterPath::FireAtVisible)
		return 0;

	const int raw = R->EAX<int>();
	g_fireAt.RawDraw = raw;

	const ShapedRadius shaped = ShapeFireAtDraw(*g_fireAt.Config, ScatterPath::FireAtVisible, raw,
		g_fireAt.Global);

	if (!shaped.Scatter)
	{
		g_fireAt.ChanceMiss = true;
		R->EAX(static_cast<DWORD>(0));
		return 0;
	}

	g_fireAt.ShapedRadius = shaped.Radius;
	R->EAX(static_cast<DWORD>(shaped.Radius));
	return 0;
}

// Visible branch: EAX = shaped * round(distance) / GetWeaponRange, ECX = that
// GetWeaponRange result (idiv leaves ECX untouched).  The engine's value is
// discarded and recomputed with the platform's Divergence ramp, normalized by
// the range of the weapon that is actually firing; ECX is only the fallback
// when the firing weapon could not be resolved at capture.
DEFINE_HOOK(0x6FE73F, Venture_Scatter_VisibleDistance, 0x6)
{
	if (!g_fireAt.Config || g_fireAt.Path != ScatterPath::FireAtVisible || g_fireAt.ChanceMiss)
		return 0;

	const int engineRange = R->ECX<int>();
	const int range = g_fireAt.Range > 0 ? g_fireAt.Range : engineRange;
	const int finalRadius = ApplyFireAtDistance(g_fireAt.ShapedRadius, g_fireAt.Distance, range,
		g_fireAt.Config->Divergence);

	g_fireAt.FinalRadius = finalRadius;
	R->EAX(static_cast<DWORD>(finalRadius));
	return 0;
}

// Legacy Inaccurate branch: EAX is the host's radius draw (RandomRanged(global/2,
// global) in the engine, or Ares' draw at 0x6FE7FE which returns here).  The
// overridden `mov edx,ds:0x00A8B230` is replayed; the engine then stores EAX as
// the radius.  This branch has no distance term anywhere, so the platform adds
// the same ramp as the visible branch.
DEFINE_HOOK(0x6FE821, Venture_Scatter_LegacyDraw, 0x6)
{
	if (!g_fireAt.Config || g_fireAt.Path != ScatterPath::FireAtLegacy)
		return 0;

	const int raw = R->EAX<int>();
	g_fireAt.RawDraw = raw;

	const ShapedRadius shaped = ShapeFireAtDraw(*g_fireAt.Config, ScatterPath::FireAtLegacy, raw,
		g_fireAt.Global);

	if (!shaped.Scatter)
	{
		g_fireAt.ChanceMiss = true;
		R->EAX(static_cast<DWORD>(0));
		return 0;
	}

	g_fireAt.ShapedRadius = shaped.Radius;
	g_fireAt.FinalRadius = g_fireAt.Range > 0
		? ApplyFireAtDistance(shaped.Radius, g_fireAt.Distance, g_fireAt.Range, g_fireAt.Config->Divergence)
		: shaped.Radius;

	R->EAX(static_cast<DWORD>(g_fireAt.FinalRadius));
	return 0;
}

// The common exit of both scatter branches: crdOffset ([esp+0x94..0x9C]) is
// final and the facing / velocity / firing solution are derived from it right
// after this point.  The 2-D ellipse is applied here, around the captured aim
// base and aligned with the shot's horizontal direction, so every downstream
// consumer sees the elliptical offset.
DEFINE_HOOK(0x6FE8EE, Venture_Scatter_FireAtExit, 0x6)
{
	if (!g_fireAt.Config)
		return 0;

	GET(BulletTypeClass*, pType, ECX);

	// Defensive: the exit belongs to the Fire_At call that captured above.
	if (pType != g_fireAt.Type)
	{
		ClearCapture();
		return 0;
	}

	REF_STACK(CoordStruct, crdOffset, 0x94);

	int offsetX = crdOffset.X - g_fireAt.BaseX;
	int offsetY = crdOffset.Y - g_fireAt.BaseY;
	const int beforeX = offsetX;
	const int beforeY = offsetY;
	bool ellipseApplied = false;

	if (!g_fireAt.ChanceMiss && g_fireAt.FinalRadius > 0
		&& NormalizeScatterAspect(g_fireAt.Config->Aspect) != 1.0)
	{
		ApplyScatterAspect(offsetX, offsetY, g_fireAt.BaseX, g_fireAt.BaseY, g_fireAt.Config->Aspect);
		crdOffset.X = g_fireAt.BaseX + offsetX;
		crdOffset.Y = g_fireAt.BaseY + offsetY;
		ellipseApplied = true;
	}

	LogFireAtShot(beforeX, beforeY, offsetX, offsetY, ellipseApplied);
	ClearCapture();

	return 0;
}

// BulletClass::MoveTo, the classic FlakScatter + Inviso branch: EAX is the
// radius the host produced (`draw * distance / range`; official Phobos / Mix /
// the fork rewrite it at this same site before this handler runs).  The
// overridden `lea ecx,[edx+0x218]` is replayed and the engine stores the
// handler's EAX into the radius slot right afterwards.  The 2-D ellipse cannot
// be applied on this path (see the file comment), so the value is radius-shaped
// only.
DEFINE_HOOK(0x4687F8, Venture_Scatter_ClassicRadius, 0x6)
{
	if (!IsEnabled())
		return 0;

	GET(BulletClass*, pThis, EBX);

	if (!pThis)
		return 0;

	const BulletTypeClass* const pType = pThis->Type;

	// This site is only reachable from the engine's FlakScatter + Inviso branch;
	// re-check the flags instead of trusting the branch.
	if (!pType || !pType->FlakScatter || !pType->Inviso)
		return 0;

	const ScatterConfig* const pConfig = FindConfig(pType);

	if (!pConfig)
		return 0;

	const int global = RulesClass::Instance->BallisticScatter;
	const int engineRadius = R->EAX<int>();
	const int range = pThis->WeaponType ? pThis->WeaponType->Range : 0;

	if (global <= 0 || range <= 0)
		return 0;

	const double distance = pThis->TargetCoords.DistanceFrom(pThis->SourceCoords);
	const double window = 2.0 * static_cast<double>(global) * distance / static_cast<double>(range);
	const double u = window > 0.0
		? std::clamp(static_cast<double>(std::max(engineRadius, 0)) / window, 0.0, 1.0)
		: 0.0;

	const ShapedRadius shaped = ShapeClassicRadius(*pConfig, engineRadius, global, distance, range);

	LogClassicShot(pType, engineRadius, u, shaped, distance, range);

	R->EAX(static_cast<DWORD>(shaped.Scatter ? shaped.Radius : 0));
	return 0;
}

// BulletClass::Explode, the Inaccurate branch's `jne 0x468FF4` (the jump over
// every target snap): the burst-point fallback for a fuse-only projectile
// (`Arm > 0`, no `ROT`), which has no guidance the platform could steer.  A
// guided missile (`ROT > 0`) is handled by the physical flight offset instead
// and deliberately skipped here.  The local burst coordinate is [esp+0xC..0x14]
// (seeded from bullet->Location at 0x468D96; the function has no frame pointer).
// The handler rewrites it and returns 0, so the replayed jump carries the new
// point into the Airburst / Cluster / Detonate paths.  Replaying the conditional
// jump is safe because the platform refuses to run without SyringeEx's
// ZFPreservation (the engine's ZF) and ReladdrInstructionFixup (the relative
// target); see src/Core/Platform.cpp.
//
// The gates are structural:
//   * Inaccurate - the engine only skips the target snap when the flag is set;
//     with it clear the snap code below would overwrite the point again;
//   * ROT <= 0 && Arm > 0 - only the fuse-only kinds: a guided missile is
//     physically scattered at launch, and scattering its burst point as well
//     would draw two offsets for one projectile;
//   * !Arcing && !Inviso - both already take an engine scatter path;
//   * both LW.BallisticScatter.Min and .Max written - the burst path has no host
//     draw window to fall back on (ScatterFeature warns about a partial pair).
// The two random draws are taken on the detonation frame from the scenario's
// synchronised RNG, so both ends of a multiplayer match compute the same point.
DEFINE_HOOK(0x468DCF, Venture_Scatter_BurstPoint, 0x6)
{
	if (!IsEnabled())
		return 0;

	GET(BulletClass*, pThis, ESI);

	if (!pThis)
		return 0;

	const BulletTypeClass* const pType = pThis->Type;

	// Every bullet explosion reaches this instruction, so the cheap flags are
	// checked before the configuration lookup.  A guided missile (`ROT > 0`) is
	// scattered at launch by the physical path instead; only a fuse-only
	// projectile reaches the shaping below.
	if (!pType || !pType->Inaccurate || pType->ROT > 0 || pType->Arm <= 0 || pType->Arcing
		|| pType->Inviso)
		return 0;

	const ScatterConfig* const pConfig = FindConfig(pType);

	if (!pConfig || !pConfig->MinSet || !pConfig->MaxSet)
		return 0;

	if (!ScenarioClass::Instance)
		return 0;

	REF_STACK(CoordStruct, coords, 0xC);

	const double distance = coords.DistanceFrom(pThis->SourceCoords);

	if (!(distance > 0.0))
		return 0;

	const int range = pThis->WeaponType ? pThis->WeaponType->Range : 0;

	// Two draws, radius fraction first and angle fraction second; both clients
	// run this exact sequence on the same frame.
	Randomizer& random = ScenarioClass::Instance->Random;
	const double u = random.RandomDouble();
	const double turn = random.RandomDouble();

	const CoordStruct direction = coords - pThis->SourceCoords;
	const BurstOffset burst = ShapeBurstOffset(*pConfig, distance, static_cast<double>(range), u, turn,
		direction.X, direction.Y);

	if (burst.Scatter)
	{
		coords.X += burst.OffsetX;
		coords.Y += burst.OffsetY;
	}

	LogBurstShot(pType, u, distance, range, *pConfig, burst, coords.X, coords.Y);

	return 0;
}

// --- physical flight offset (B16) ------------------------------------------
//
// A ROT > 0 missile that writes both LW.BallisticScatter.Min and .Max is steered
// at "target centre + offset" for its whole flight and set off at that point, so
// it really flies to the scattered point instead of only exploding off-centre.
// The offset is drawn once, at launch; the flight path only reads it.  There is
// no separate opt-in key: using the platform's scatter keys on a guided missile
// is the whole switch (user decision 2026-11-20 - the fork's Physical=yes split
// is deliberately not ported).
//
//   0x4688A9 / 6 - BulletClass::MoveTo's merge point (the classic FlakScatter +
//                  Inviso branch joins the plain path here).  TargetCoords and
//                  SourceCoords are final, so this is where the offset is drawn
//                  and the per-bullet state registered.  The official Phobos /
//                  Mix handlers at this address return non-zero only for Inviso
//                  bullets; a physical missile is never Inviso, so this handler
//                  runs last in the chain.
//   0x466BAF / 6 - per frame, inside the engine's guidance block: the aim point
//                  it just computed ([esp+0x30..0x38]) is moved by the offset
//                  and written back, and the proximity fuse's reference point
//                  (Data.Location / Data.Distance) follows it.  The overridden
//                  `mov ecx,ds:0x8871E0` is replayed through R->ECX.
//   0x4677D3 / 5 - per frame, at the head of the engine's detonation chain: the
//                  missile is set off on the frame whose flight segment passes
//                  closest to the aim point (segment test, so a fast missile
//                  cannot jump over it).  The official Phobos / Mix handlers
//                  return non-zero only for projectiles that carry a trajectory
//                  of their own; a physical missile has none.
//   0x4665E9 / 0xA - BulletClass destructor: drop the per-bullet state (the
//                  table is feature-local and non-savegame; a scenario clear
//                  also empties it, see ScatterFeature.cpp).

// BulletClass::MoveTo merge point: draw the physical scatter offset and register
// the per-bullet state.  EBX = BulletClass*.
DEFINE_HOOK(0x4688A9, Venture_Scatter_PhysicalInit, 0x6)
{
	if (!IsEnabled())
		return 0;

	GET(BulletClass*, pThis, EBX);

	if (!pThis)
		return 0;

	const BulletTypeClass* const pType = pThis->Type;

	// Only an engine-guided missile can be steered off its target: the guidance
	// block opens on ROT > 0.  Inaccurate keeps the engine's burst-point snaps
	// away from the final position (0x468DCF jumps over them); Arcing / Inviso
	// already take an engine scatter path, so they must not be scattered twice.
	if (!pType || pType->ROT <= 0 || !pType->Inaccurate || pType->Arcing || pType->Inviso)
		return 0;

	const ScatterConfig* const pConfig = FindConfig(pType);

	if (!pConfig || !pConfig->MinSet || !pConfig->MaxSet)
		return 0;

	// MoveTo also runs when a bullet is repositioned: draw the offset once and
	// never consume random numbers for a bullet that already has its state.
	if (FetchPhysicalScatter(pThis))
		return 0;

	// Unlimbo wrote TargetCoords a few instructions earlier; a zero point means
	// there is no aim to scatter around.
	const CoordStruct base = pThis->TargetCoords;

	if (base == CoordStruct::Empty)
		return 0;

	if (!ScenarioClass::Instance)
		return 0;

	const double distance = base.DistanceFrom(pThis->SourceCoords);

	if (!(distance > 0.0))
		return 0;

	const int range = pThis->WeaponType ? pThis->WeaponType->Range : 0;

	// One draw, radius fraction first and angle fraction second; both clients
	// take the same two draws on the launch frame.
	Randomizer& random = ScenarioClass::Instance->Random;
	const double u = random.RandomDouble();
	const double turn = random.RandomDouble();

	const CoordStruct direction = base - pThis->SourceCoords;
	const BurstOffset offset = ShapeBurstOffset(*pConfig, distance, static_cast<double>(range), u, turn,
		direction.X, direction.Y);

	if (!offset.Scatter)
	{
		LogPhysicalLaunch(pType, u, distance, range, *pConfig, offset, base, base, false);
		return 0;
	}

	CoordStruct aim{base.X + offset.OffsetX, base.Y + offset.OffsetY, base.Z};

	// Keep the aim on the map: an illegal bullet position makes the engine remove
	// the bullet silently (no explosion, no damage).  Only the aim is clamped,
	// not the configured radius.
	const LTRBStruct& mapBounds = MapClass::Instance.MapCoordBounds;
	const ScatterAimBounds aimBounds{
		(mapBounds.Left + 1) * kLeptonsPerCell + kLeptonsPerCell / 2,
		(mapBounds.Right - 1) * kLeptonsPerCell + kLeptonsPerCell / 2,
		(mapBounds.Top + 1) * kLeptonsPerCell + kLeptonsPerCell / 2,
		(mapBounds.Bottom - 1) * kLeptonsPerCell + kLeptonsPerCell / 2};
	const bool clamped = ClampScatterAim(aim.X, aim.Y, aimBounds);

	PhysicalScatterState state;
	state.OffsetX = aim.X - base.X;
	state.OffsetY = aim.Y - base.Y;
	state.AimX = aim.X;
	state.AimY = aim.Y;
	state.AimZ = aim.Z;
	state.ShapedRadius = offset.ShapedRadius;
	state.Radius = offset.Radius;

	if (!BeginPhysicalScatter(pThis, state))
		return 0;

	LogPhysicalLaunch(pType, u, distance, range, *pConfig, offset, base, aim, clamped);
	return 0;
}

// BulletClass::Update, the guidance block: the engine has just worked out this
// frame's aim point (target centre, or the building's target coordinate) and is
// about to turn it into a steering direction.  Moving it here is what makes the
// missile fly to the scattered point.  EBP = BulletClass*; the overridden
// `mov ecx,ds:0x8871E0` is the function's RulesClass load.
DEFINE_HOOK(0x466BAF, Venture_Scatter_PhysicalAim, 0x6)
{
	enum { Continue = 0x466BB5 };

	GET(BulletClass*, pThis, EBP);

	if (PhysicalScatterState* const pState = FetchPhysicalScatter(pThis))
	{
		REF_STACK(CoordStruct, aim, 0x30);

		if (pThis->Target)
		{
			aim.X += pState->OffsetX;
			aim.Y += pState->OffsetY;
		}
		else
		{
			// Target lost: the engine aims at the global null coordinate now.
			// Keep flying to the point the missile was already steering at.
			aim.X = pState->AimX;
			aim.Y = pState->AimY;
			aim.Z = pState->AimZ;
		}

		pState->AimX = aim.X;
		pState->AimY = aim.Y;
		pState->AimZ = aim.Z;

		// The engine's proximity fuse reads both of these; centre it on the
		// scattered point instead of the target centre.
		pThis->Data.Location = aim;
		pThis->Data.Distance = static_cast<int>(pThis->Location.DistanceFrom(aim));
	}

	R->ECX(reinterpret_cast<DWORD>(RulesClass::Instance));
	return Continue;
}

// BulletClass::Update, head of the detonation chain: set a physical-scatter
// missile off where it is steering.  The engine's own rules are built around the
// target centre, so this is the primary trigger; the fuse (Data.Location) is
// only a second line of defence.  The test is a segment test: the point on this
// frame's flight path closest to the aim, so a fast missile cannot jump over the
// aim between two frames.  EBP = BulletClass*; [esp+0x24..0x2C] is the position
// this frame moved to.
DEFINE_HOOK(0x4677D3, Venture_Scatter_PhysicalDetonate, 0x5)
{
	enum { Detonate = 0x467E53 };

	GET(BulletClass*, pThis, EBP);

	PhysicalScatterState* const pState = FetchPhysicalScatter(pThis);

	// The fuse must have armed first; for an aircraft target the engine forces
	// the arm time to 0, so anti-air shots are armed on the launch frame.
	if (!pState || !pThis->Data.ArmTimer.Expired())
		return 0;

	REF_STACK(const CoordStruct, position, 0x24);

	const CoordStruct aim{pState->AimX, pState->AimY, pState->AimZ};
	const BulletVelocity& velocity = pThis->Velocity;
	const double toAimX = static_cast<double>(aim.X - position.X);
	const double toAimY = static_cast<double>(aim.Y - position.Y);
	const double toAimZ = static_cast<double>(aim.Z - position.Z);
	const double speedSquared = velocity.X * velocity.X + velocity.Y * velocity.Y + velocity.Z * velocity.Z;
	const double projection = speedSquared > 0.0
		? (toAimX * velocity.X + toAimY * velocity.Y + toAimZ * velocity.Z) / speedSquared
		: -1.0;

	// Closest point to the aim on this frame's path: 0 = where the frame started,
	// 1 = the position the engine moved the bullet to.
	CoordStruct closest = position;

	if (projection >= 1.0)
	{
		closest.X += static_cast<int>(velocity.X);
		closest.Y += static_cast<int>(velocity.Y);
		closest.Z += static_cast<int>(velocity.Z);
	}
	else if (projection > 0.0)
	{
		closest.X += static_cast<int>(velocity.X * projection);
		closest.Y += static_cast<int>(velocity.Y * projection);
		closest.Z += static_cast<int>(velocity.Z * projection);
	}

	const double closestDistance = closest.DistanceFrom(aim);
	const double step = std::sqrt(speedSquared);

	if (projection <= 1.0 && closestDistance <= 32.0)
	{
		pThis->SetLocation(MapClass::Instance.CoordinatesLegal(closest) ? closest : aim);
		LogPhysicalDetonate(pThis->Type, pThis->Location, aim, closestDistance);
		return Detonate;
	}

	// Already past the aim (the previous frame's test can miss a fast shot).
	if (projection < 0.0 && position.DistanceFrom(aim) <= step)
	{
		pThis->SetLocation(MapClass::Instance.CoordinatesLegal(position) ? position : aim);
		LogPhysicalDetonate(pThis->Type, pThis->Location, aim, position.DistanceFrom(aim));
		return Detonate;
	}

	return 0;
}

// BulletClass destructor: drop the per-bullet physical-scatter state.  The
// window is the single 10-byte `mov dword [esi+0xAC],0` (two objdump lines);
// ESI = BulletClass (ECX is not reliable here).  Ares / official Phobos /
// Kratos / Mix / the fork hook the same address with size 0xA and return 0.
DEFINE_HOOK(0x4665E9, Venture_Scatter_PhysicalDTOR, 0xA)
{
	if (!IsEnabled())
		return 0;

	GET(BulletClass*, pThis, ESI);

	if (pThis)
		EndPhysicalScatter(pThis);

	return 0;
}

// BulletTypeClass::LoadFromINI success epilogue: the engine has read the
// projectile's section, ESI is the BulletTypeClass and the CCINIClass is at
// [esp+0x90].  Ares / Kratos hook both the success and the failure epilogue and
// the official Phobos / Mix only the success one; all return 0, so the platform
// chains with them.  The failure path (0x46C429) has no section to read and is
// deliberately not hooked.
DEFINE_HOOK(0x46C41C, Venture_Scatter_BulletType_INI, 0xA)
{
	if (!IsEnabled())
		return 0;

	GET(BulletTypeClass*, pType, ESI);
	GET_STACK(CCINIClass*, pINI, 0x90);

	if (pType && pINI)
		LoadScatterConfigFromIni(pType, pINI);

	return 0;
}
