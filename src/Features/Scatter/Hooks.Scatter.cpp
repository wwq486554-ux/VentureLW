// ---------------------------------------------------------------------------
// Scatter core feature module (P1 / R10) - the engine hooks.
//
// The module shapes the scatter the host has already produced; it never draws a
// random number of its own (see ScatterMath.h).  The sites and their windows
// were audited instruction by instruction against the supported gamemd build
// (the round's engine-site record in the workspace) and against the Ares /
// Kratos / official Phobos / Mix hook tables:
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
using venture::features::scatter::FindConfig;
using venture::features::scatter::HostBounds;
using venture::features::scatter::IsEnabled;
using venture::features::scatter::LoadScatterConfigFromIni;
using venture::features::scatter::NormalizeDraw;
using venture::features::scatter::NormalizeScatterAspect;
using venture::features::scatter::ScatterConfig;
using venture::features::scatter::ScatterPath;
using venture::features::scatter::ShapeClassicRadius;
using venture::features::scatter::ShapeFireAtDraw;
using venture::features::scatter::ShapedRadius;
using venture::Log;

namespace
{
// Diagnostics: bounded number of per-shot debug lines, so an in-game check can
// see exactly what the shaping did without flooding a long battle's log.
constexpr int kShotLogLimit = 60;
constexpr int kUnconfiguredNoteLimit = 16;
int g_shotLogs = 0;
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

// One bounded debug line per shaped shot, printed at the scatter block's exit
// where the final offset is known.  Only called for shots whose projectile type
// carries LW.* keys, so an unconfigured battle never fills the log.
void LogFireAtShot(const int beforeX, const int beforeY, const int afterX, const int afterY,
	const bool ellipseApplied)
{
	if (g_shotLogs >= kShotLogLimit)
	{
		if (g_shotLogs == kShotLogLimit)
		{
			++g_shotLogs;
			Log::Info("[Scatter] shaped-shot debug lines capped at %d for this session", kShotLogLimit);
		}

		return;
	}

	++g_shotLogs;

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
	if (g_shotLogs >= kShotLogLimit)
	{
		if (g_shotLogs == kShotLogLimit)
		{
			++g_shotLogs;
			Log::Info("[Scatter] shaped-shot debug lines capped at %d for this session", kShotLogLimit);
		}

		return;
	}

	++g_shotLogs;

	Log::Debug("[Scatter] shot path=%s type=%s engine_radius=%d u=%.3f shaped=%d dist=%.0f range=%d "
			   "-> %s",
		PathName(ScatterPath::Classic), pType->ID, engineRadius, u, shaped.Radius, distance, range,
		shaped.Scatter ? "shaped" : "no scatter (chance miss)");
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
