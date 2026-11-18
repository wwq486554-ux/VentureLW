// ---------------------------------------------------------------------------
// TemporalAnySlot feature: a Temporal warhead works from any weapon slot, and
// a running warp erases at the rate of the weapon that actually delivered it.
//
// Ported from Phobos-LWModified commits c2341b52 (see
// Waterline/已实现/一期/SpecialAction功能实现记录.md §11 and
// Waterline/已实现/二期/超时空武器体系实现记录.md §2.1), where the fix lives in
// the fork's own copy of the two engine routines.  The platform cannot take the
// fork's route: the official Phobos alpha2 already replaces
// TemporalClass::GetWarpPerStep (0x71AB10) outright, and Ares replaces the middle
// of TemporalClass::Update (its 0x71A84E hook resumes at 0x71A88D), so the
// platform uses two injection points that both hosts leave alone:
//
//   1. 0x4694BB (size 6) - BulletClass::Detonate, right before the engine calls
//      TemporalClass::Fire on Owner->TemporalImUsing (0x4694C1).  The engine
//      only creates that instance in TechnoClass::Init and only when weapon slot
//      0's warhead is Temporal (0x6F4154-0x6F41A3), then dereferences it here
//      without a null check -> the known crash at 0x71AF4D.  Creating it on the
//      first Temporal detonation covers every path (any slot, AirburstWeapon,
//      ShrapnelWeapon, ForceWeapon, Ares superweapons); the constructor is
//      deterministic (registers itself in TemporalClass::Array, draws no random
//      numbers) and the stolen `mov ecx,[ecx+0x274]` is replayed, so the engine's
//      own Fire call picks the new instance up.  The same hook records the
//      weapon that delivered the shot in the platform's per-techno extension.
//
//   2. 0x71AB08 (size 7) - the shared tail of TemporalClass::Update.  The
//      engine's continue path (`0x71A88F jg 0x71AB08`) and the Ares path
//      (Ares's TemporalClass_UpdateA hook at 0x71A84E computes the frame and
//      resumes at 0x71A88D, whose Phobos hook returns 0x71AB08) both run it, so
//      it is the one game-side point that sees every host's per-frame result.
//      The erase rate is derived from the techno's *current* weapon selection,
//      which after a forced-slot/any-slot shot is not the weapon that fired:
//      WarpRemaining is then drained by the wrong Damage (the fork measured
//      20 instead of 1000, i.e. 105 frames instead of 3).  Here the frame's
//      engine step per chain node (TemporalClass::WarpPerStep, written by
//      whichever GetWarpPerStep implementation is active, so official Phobos's
//      Temporal.ApplyMultiplier / ApplyVersus factors are already folded in)
//      is scaled by recordedDamage / engineDamage and the difference is added
//      back to WarpRemaining.  A node whose engine-selected weapon already has
//      a Temporal warhead is left alone - vanilla Chrono Legionnaire/Chrono Tank
//      behaviour and Ares's WeaponIndex_Warp usage are unchanged.
//
// Known boundary (documented in docs/Fixed-or-Improved-Logics.md): the
// correction is proportional, so the engine-selected weapon's warhead factors
// (Temporal.ApplyVersus and friends from a co-loaded official Phobos) keep their
// own contribution; the fork's whole-function rewrite replaces the weapon and
// its warhead together.  The rate follows the fired weapon's Damage, which is
// what the fix is about.
//
// No INI keys, no savegame state (the recorded weapon is transient and lives in
// the non-saveable TechnoDataExt), [Features] TemporalAnySlot gates the whole
// module (default on).
// ---------------------------------------------------------------------------

#include "Core/Log.h"
#include "Engine/EngineCompat.h"
#include "Ext/TechnoDataExt.h"

#include <platform/Feature.h>

namespace venture::features::temporal_any_slot
{
namespace
{
// Written by the platform before Initialize() runs (see FeatureContext).
bool g_enabled = false;

// Ares is detected once at startup.  Its techno extension carries the weapon
// index the engine uses for a running warp (WeaponIndex_Warp, +0x0A inside the
// Ares extension).  With Ares absent the engine path asks SelectWeapon(nullptr).
// Same host gate the official Phobos replacement uses; see
// docs/HostMatrix.md and the D2 decision in the R2 review record.
bool g_hasAres = false;

// The engine's own cap: GetWarpPerStep (0x71AB10) stops recursing past helper
// count 50, and the head instance is processed on top of that.
constexpr int kMaxChainNodes = 51;

// One line per warp is enough evidence for the in-game check; the counters keep
// a long battle from flooding the log.
constexpr int kFixLogLimit = 8;
int g_fixLogs = 0;
int g_aresNullLogs = 0;

unsigned PtrOf(const void* const pointer)
{
	return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(pointer));
}

// The weapon the engine currently uses to erase with, resolved exactly like the
// active GetWarpPerStep implementation does (Ares field when Ares is loaded, the
// techno's own selection otherwise).
WeaponTypeClass* EngineWarpWeapon(TechnoClass* const pOwner)
{
	if (!pOwner)
		return nullptr;

	int index = 0;

	if (g_hasAres)
	{
		void* const pAresExt =
			reinterpret_cast<void*>(static_cast<std::uintptr_t>(pOwner->align_154));

		if (!pAresExt)
		{
			if (g_aresNullLogs < 4)
			{
				++g_aresNullLogs;
				Log::Debug("[TemporalAnySlot] Ares extension is null for TechnoClass 0x%08X",
					PtrOf(pOwner));
			}
			return nullptr;
		}

		// Ares' TechnoClass extension: WeaponIndex_Warp is the byte at +0x0A.
		index = *reinterpret_cast<const unsigned char*>(static_cast<const char*>(pAresExt) + 0x0A);
	}
	else
	{
		index = pOwner->SelectWeapon(nullptr);
	}

	if (index < 0)
		return nullptr;

	WeaponStruct* const pSlot = pOwner->GetWeapon(index);
	return pSlot ? pSlot->WeaponType : nullptr;
}

// The 0x71AB08 correction.  The engine just subtracted the per-frame step of
// every chain node from this instance's WarpRemaining; for each node whose
// engine-selected weapon is not Temporal while this techno did deliver a
// Temporal warhead, the step the engine used is rescaled to the fired weapon's
// Damage and the difference is added back.
void ApplyWarpStepFixup(TemporalClass* const pThis)
{
	int delta = 0;
	int nodes = 0;
	int correctedNodes = 0;

	for (TemporalClass* pNode = pThis; pNode != nullptr && nodes < kMaxChainNodes;
		pNode = pNode->PrevTemporal)
	{
		++nodes;

		TechnoClass* const pOwner = pNode->Owner;
		int const actual = pNode->WarpPerStep;

		if (!pOwner || actual <= 0)
			continue;

		TechnoDataExt* const pExt = TechnoDataExt::Fetch(pOwner);

		if (!pExt)
			continue;

		WeaponTypeClass* const pRecorded = pExt->TemporalWarpWeapon;

		if (!pRecorded || !pRecorded->Warhead || !pRecorded->Warhead->Temporal)
			continue;

		int const recordedDamage = pRecorded->Damage;

		if (recordedDamage <= 0)
			continue;

		WeaponTypeClass* const pEngine = EngineWarpWeapon(pOwner);

		if (!pEngine || pEngine->Damage <= 0)
			continue;

		// The engine already picked a Temporal weapon: leave its rate alone.
		if (pEngine->Warhead && pEngine->Warhead->Temporal)
			continue;

		int const desired = static_cast<int>(
			(static_cast<long long>(actual) * recordedDamage) / pEngine->Damage);

		if (desired <= 0 || desired == actual)
			continue;

		delta += actual - desired;
		++correctedNodes;
	}

	if (delta == 0)
		return;

	pThis->WarpRemaining += delta;

	if (g_fixLogs < kFixLogLimit)
	{
		++g_fixLogs;
		Log::Info("[TemporalAnySlot] warp rate corrected: TemporalClass 0x%08X, %d node(s), "
			"WarpRemaining %+d -> %d",
			PtrOf(pThis), correctedNodes, delta, pThis->WarpRemaining);
	}
}

// The 0x4694BB part: remember the weapon of the shot and make sure the owner has
// the Temporal instance the engine is about to dereference.
void EnsureTemporalInstance(TechnoClass* const pOwner, WeaponTypeClass* const pWeapon)
{
	if (!pOwner)
		return;

	if (TechnoDataExt* const pExt = TechnoDataExt::Fetch(pOwner))
	{
		if (pWeapon)
			pExt->TemporalWarpWeapon = pWeapon;
	}

	if (pOwner->TemporalImUsing)
		return;

	pOwner->TemporalImUsing = GameCreate<TemporalClass>(pOwner);

	Log::Debug("[TemporalAnySlot] created TemporalClass for TechnoClass 0x%08X (slot-0 warhead "
		"was not Temporal)", PtrOf(pOwner));
}

void __cdecl Initialize(const FeatureContext& context)
{
	g_hasAres = context.HasHost && context.HasHost(Host::Ares);

	Log::Info("[TemporalAnySlot] initialized (enabled=%d, ares=%d)", g_enabled ? 1 : 0,
		g_hasAres ? 1 : 0);
}

const FeatureDescriptor kDescriptor{
	"temporal_any_slot", // id: internal identifier
	"TemporalAnySlot",   // iniKey: switch in [Features]
	"TemporalAnySlot: Temporal warheads from any weapon slot, erased at the fired weapon's rate",
	0, // requiredHosts (AND): no host dependency
	0, // anyOfHosts (OR): no host dependency
	&g_enabled,
	&Initialize,
};
} // namespace

bool IsEnabled()
{
	return g_enabled;
}

VENTURE_REGISTER_FEATURE(kDescriptor);
} // namespace venture::features::temporal_any_slot

// --- hooks -----------------------------------------------------------------

// BulletClass::Detonate, immediately before the engine calls TemporalClass::Fire
// on Owner->TemporalImUsing.  ECX already holds the bullet's owner (loaded at
// 0x4694B4), EDI the warp target (not needed here).  Returning 0 replays the
// stolen `mov ecx,[ecx+0x274]` with ECX untouched, so the call at 0x4694C1 picks
// up the instance created here.
DEFINE_HOOK(0x4694BB, Venture_TemporalAnySlot_Detonate, 0x6)
{
	GET(BulletClass*, pBullet, ESI);
	GET(TechnoClass*, pOwner, ECX);

	if (!venture::features::temporal_any_slot::IsEnabled())
		return 0;

	venture::features::temporal_any_slot::EnsureTemporalInstance(pOwner, pBullet->WeaponType);
	return 0;
}

// TemporalClass::Update tail: `pop edi; pop esi; pop ebp; pop ebx; add esp,0x24`
// (7 bytes, next instruction 0x71AB0F `ret`).  ESI is the TemporalClass; the
// handler runs on both the continue and the erase paths, so the erase path
// (WarpRemaining already <= 0) is skipped.
DEFINE_HOOK(0x71AB08, Venture_TemporalAnySlot_WarpStep, 0x7)
{
	GET(TemporalClass*, pThis, ESI);

	if (!venture::features::temporal_any_slot::IsEnabled() || !pThis || pThis->WarpRemaining <= 0)
		return 0;

	venture::features::temporal_any_slot::ApplyWarpStepFixup(pThis);
	return 0;
}
