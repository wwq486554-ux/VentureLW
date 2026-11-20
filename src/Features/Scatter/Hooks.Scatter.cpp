// ---------------------------------------------------------------------------
// Scatter core feature module (P1 / R10) - the engine hooks.
//
// Current state (first commit of the round): a READ-ONLY probe of the host
// scatter sample.  The design document forbids guessing the sample shape
// (square vs disc, radial uniformity) before the mapping is written, so this
// build observes what the co-loaded host (engine + Ares [+ official Phobos])
// actually produces:
//
//   Fire_At path (the engine's own scatter block, both branches):
//     0x6FE67D / 6 - the block decided to run (Inaccurate && Arcing): capture the
//                    bullet type, the unscattered aim base ([esp+0x30..0x38]) and
//                    the 3-D distance for the exit line.
//     0x6FE71C / 6 - visible branch: EAX is the radius draw the host produced.
//     0x6FE821 / 6 - legacy Inaccurate branch: same.
//     0x6FE8EE / 6 - the block's exit, before facing/velocity are derived: the
//                    engine has written the scattered offset into crdOffset
//                    ([esp+0x94..0x9C]); log offset = crdOffset - aim base.
//
// Every handler returns 0, i.e. the overridden instructions are replayed by the
// trampoline - the probe cannot change behaviour.
//
// Co-load notes that decide the site choice (this round's audit):
//   * Ares 3.0 owns 0x6FE709 (visible) and 0x6FE7FE (legacy) and jumps with a
//     non-zero return straight into 0x6FE71C / 0x6FE821, so the draw is read at
//     those addresses, never at Ares's (0x6FE67D, the capture, is before Ares's
//     first window, so it runs exactly once).
//   * 0x6FE67D and 0x6FE8EE are free in the audited Ares / official Phobos /
//     Kratos / Mix hook tables; only the fork declares them (a separate,
//     lineage-exclusive build that cannot be co-loaded).
//   * The classic FlakScatter+Inviso path (BulletClass::MoveTo 0x4687F8 /
//     0x4688A9) is NOT probed in this build: its post-scatter sites are owned by
//     Ares (0x4688BD always returns 0x468A3F) and the official Phobos / Mix
//     obstacle handler (0x4688A9 returns 0x4688BD for Inviso bullets), so a
//     chained handler there would never run.  The round therefore shapes that
//     path at the shared 0x4687F8 radius site (all host handlers there return 0
//     and the platform is last in the chain, so the remap sees the host's final
//     radius); the 2-D ellipse cannot be applied on that path and the limitation
//     is documented in the handover manual and in the INI guide.
//
// The probe is gated on `[Scatter] Probe=yes`, caps 120 samples per projectile
// type (the cap is logged once), and writes at debug level.  All of it is
// temporary and removed when the round closes; the addresses/sizes stay and the
// handler bodies are replaced by the real shaping.
// ---------------------------------------------------------------------------

#include <Syringe.h>
#include <SpecificStructures.h>

#include <Helpers/Macro.h>

#include <cmath>
#include <unordered_map>

#include "Core/Log.h"
#include "Engine/EngineCompat.h"
#include "Features/Scatter/Scatter.h"

using venture::features::scatter::HasAres;
using venture::features::scatter::IsEnabled;
using venture::features::scatter::IsProbeEnabled;
using venture::Log;

namespace
{
// One Fire_At call is a single synchronous pass through the scatter block and
// the game runs the logic on one thread, so a file-static capture is enough.
struct FireAtCapture
{
	const BulletTypeClass* Type = nullptr;
	int BaseX = 0;
	int BaseY = 0;
	int BaseZ = 0;
	int Distance = 0;
	int RawDraw = -1;
	bool Active = false;
};

FireAtCapture g_fireAt;

constexpr int kProbeSamplesPerType = 120;

std::unordered_map<const BulletTypeClass*, int> g_fireAtSamples;

// Returns true while this projectile type is still under the sample cap; logs
// the cap line exactly once.
bool ShouldLog(std::unordered_map<const BulletTypeClass*, int>& counts,
	const BulletTypeClass* const type, const char* const path)
{
	if (!type)
		return false;

	int& count = counts[type];

	if (count < kProbeSamplesPerType)
	{
		++count;
		return true;
	}

	if (count == kProbeSamplesPerType)
	{
		++count;
		Log::Info("[ScatterProbe] %s samples for projectile '%s' capped at %d - further samples are suppressed",
			path, type->ID, kProbeSamplesPerType);
	}

	return false;
}

// The engine picks the Fire_At branch by exactly these flags (0x6FE691 /
// 0x6FE69F; in BulletClass::MoveTo the same pair is checked at 0x468734 /
// 0x468741).
const char* BranchName(const BulletTypeClass* const type)
{
	return (type && type->FlakScatter && !type->Inviso) ? "visible" : "legacy";
}
} // namespace

// TechnoClass::Fire_At, the engine's scatter block decided to run
// (`je 0x6FE8EE` when !Inaccurate; the Arcing check follows).  ECX is the
// BulletTypeClass, the unscattered aim offset (muzzle -> aim) is at
// [esp+0x30..0x38] and is copied into crdOffset right before this point.
DEFINE_HOOK(0x6FE67D, Venture_Scatter_Capture, 0x6)
{
	g_fireAt.Active = false;

	if (!IsEnabled() || !IsProbeEnabled())
		return 0;

	GET(BulletTypeClass*, pType, ECX);

	if (!pType || !pType->Inaccurate || !pType->Arcing)
		return 0;

	g_fireAt.Type = pType;
	g_fireAt.BaseX = R->Stack<int>(0x30);
	g_fireAt.BaseY = R->Stack<int>(0x34);
	g_fireAt.BaseZ = R->Stack<int>(0x38);

	const double dx = static_cast<double>(g_fireAt.BaseX);
	const double dy = static_cast<double>(g_fireAt.BaseY);
	const double dz = static_cast<double>(g_fireAt.BaseZ);
	g_fireAt.Distance = static_cast<int>(std::sqrt(dx * dx + dy * dy + dz * dz));
	g_fireAt.RawDraw = -1;
	g_fireAt.Active = true;

	return 0;
}

// Visible branch: the host's radius draw has just returned in EAX (Ares, when
// loaded, replaced the engine's own draw at 0x6FE709 and returns to this
// address).  The engine's next two instructions - `fld [esp+0x18]` (4 B) and
// `mov ebx,eax` (2 B) - are replayed unchanged; the probe only records the
// value.  This is the one window that covers two instructions, so its size must
// stay 6 (5 would cut `mov ebx,eax` in half).
DEFINE_HOOK(0x6FE71C, Venture_Scatter_VisibleDraw, 0x6)
{
	if (!g_fireAt.Active || !IsEnabled() || !IsProbeEnabled())
		return 0;

	g_fireAt.RawDraw = R->EAX<int>();

	return 0;
}

// Legacy Inaccurate branch (reached for !FlakScatter or Inviso): EAX is the
// host's radius draw; the overridden `mov edx,ds:0x00A8B230` (6 B, one
// instruction) is replayed.
DEFINE_HOOK(0x6FE821, Venture_Scatter_LegacyDraw, 0x6)
{
	if (!g_fireAt.Active || !IsEnabled() || !IsProbeEnabled())
		return 0;

	g_fireAt.RawDraw = R->EAX<int>();

	return 0;
}

// Exit of the whole scatter block, before facing and the firing solution are
// derived from crdOffset - so the value logged here is exactly what the host
// delivered: X at [esp+0x94], Y at [esp+0x98], Z at [esp+0x9C] (Z stays the aim
// base Z).  The overridden `mov edx,[ecx+0x2DC]` (6 B) is replayed.
DEFINE_HOOK(0x6FE8EE, Venture_Scatter_FireAtExit, 0x6)
{
	if (!g_fireAt.Active || !IsEnabled() || !IsProbeEnabled())
		return 0;

	GET(BulletTypeClass*, pType, ECX);

	if (pType != g_fireAt.Type)
	{
		g_fireAt.Active = false;
		return 0;
	}

	const int offsetX = R->Stack<int>(0x94) - g_fireAt.BaseX;
	const int offsetY = R->Stack<int>(0x98) - g_fireAt.BaseY;
	const int global = RulesClass::Instance->BallisticScatter;

	if (ShouldLog(g_fireAtSamples, pType, "fire_at"))
	{
		Log::Debug("[ScatterProbe] path=fire_at branch=%s type=%s raw=%d global=%d dist=%d dx=%d dy=%d ares=%d",
			BranchName(pType), pType->ID, g_fireAt.RawDraw, global, g_fireAt.Distance,
			offsetX, offsetY, HasAres() ? 1 : 0);
	}

	g_fireAt.Active = false;

	return 0;
}
