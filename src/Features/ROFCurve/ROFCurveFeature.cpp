// ---------------------------------------------------------------------------
// ROFCurve feature: weapon-level spin-up (the weapon fires faster - or slower -
// the longer it keeps firing).
//
// Ported from Phobos-LWModified commits 47023891 / b950ac50 (curve) and
// 6c54bd6f (one chain per weapon, "B18"), see
// Waterline/已实现/二期/ROF曲线实现记录.md and
// ROF曲线多武器独立计数器实现记录.md.  Only the interval between two shots
// changes: the projectile, animation, report, warhead and damage are untouched,
// and no weapon is ever swapped.
//
// Injection points (both free in the audited ecosystem hook tables):
//
//   0x6FF2AA (size 6) - TechnoClass::Fire_At, the branch every non-DiskLaser
//     weapon takes: the engine has just produced the reload delay in EAX
//     (CalculateROF at 0x6FF289 plus the berzerk halving) and is about to store
//     it into RearmTimer.TimeLeft (+0x2F4) at 0x6FF2BB.  Rewriting EAX here
//     changes exactly the delay the engine applies.
//   0x6FE4B0 (size 6) - the same instruction on the DiskLaser=yes side of the
//     branch at 0x6FE468.  The fork read the weapon from EBX here, but on this
//     branch EBX already holds the DiskLaserClass* created at 0x6FE483, so the
//     fork's curve silently never applied to DiskLaser weapons.  The platform
//     takes the weapon from the function's own argument instead
//     (`GetWeapon([ebp+0xC])`, exactly what the engine did at 0x6FDD61-0x6FDD6F
//     to obtain its weapon), which is correct on both branches and needs no
//     stack-layout assumption.
//
//   The curve is applied as a ratio to the engine's delay, so every other
//   modifier the engine folded in (AttachEffect ROF multiplier, veterancy,
//   ROF.RandomDelay, berzerk) keeps working; only the pace itself changes.
//   The berzerk halving is kept, unlike in the fork's SweepFire case, because
//   nothing else rewrites EAX on this path.
//
// Type-level state: the seven keys are read per weapon section in the
// WeaponTypeClass::LoadFromINI epilogue (0x7729B0 / 0x7729C7, size 5 - the same
// sites the official Phobos, Ares, Kratos and Mix hook, all with an all-zero
// handler, so a co-loaded platform chains).  The resolved configuration is kept
// in a feature-local table keyed by the weapon type.  It deliberately does NOT
// use the object extension store: weapon types are process-lifetime
// configuration objects, while the platform's extension stores are cleared on
// scenario clear (and the savegame load path runs that same clear), so a type
// store would lose its data on every savegame load and the rules are only parsed
// once.  The table is written only when the rules are parsed and read on every
// shot; no savegame state is involved.
//
// Techno-level state: the per-weapon chains live in the shared TechnoDataExt
// (src/Ext/TechnoDataExt.h) - a game object carries one platform extension, so a
// feature must not declare its own TechnoClass store.  The chains are transient
// (deliberately not serialized): a save/load starts the curves over, which is
// what they look like anyway.
//
// Behaviour is opt-in: a weapon without `ROFCurve=yes` and `ROFCurve.End` keeps
// its normal rate of fire and owns no chain.  The whole module is
// `[Features] ROFCurve` (default on).
// ---------------------------------------------------------------------------

#include "Core/EngineIni.h"
#include "Core/Log.h"
#include "Engine/EngineCompat.h"
#include "Ext/TechnoDataExt.h"

#include "Features/ROFCurve/ROFCurveMath.h"

#include <platform/Feature.h>

#include <unordered_map>

namespace venture::features::rof_curve
{
namespace
{
// Written by the platform before Initialize() runs (see FeatureContext).
bool g_enabled = false;

// Resolved per-weapon configuration, filled once when rules(md).ini is parsed.
// Weapon types are process-lifetime objects, so the pointer keys are stable; a
// re-parse (if the engine ever does one) simply overwrites the entry.
std::unordered_map<const WeaponTypeClass*, ROFCurveConfig> g_weaponCurves;

// One INFO line per applied curve is enough evidence for the in-game check; the
// counters keep a long battle from flooding the log.
constexpr int kApplyLogLimit = 8;
int g_applyLogs = 0;
int g_missingExtWarned = 0;

unsigned PtrOf(const void* const pointer)
{
	return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(pointer));
}

const ROFCurveConfig* FindWeaponCurve(const WeaponTypeClass* const pWeapon)
{
	if (!pWeapon)
		return nullptr;

	auto const it = g_weaponCurves.find(pWeapon);
	return it == g_weaponCurves.end() ? nullptr : &it->second;
}

// Reads this weapon's seven keys and resolves the curve.  Called from the
// WeaponTypeClass::LoadFromINI epilogue, i.e. after the engine itself read the
// section (so `pItem->ROF` is set) and before anything can fire.
void LoadWeaponCurveFromIni(WeaponTypeClass* const pWeapon, CCINIClass* const pINI)
{
	if (!pWeapon || !pINI)
		return;

	const char* const section = pWeapon->ID;

	if (!section || !*section)
		return;

	ROFCurveConfig config;
	config.Enabled = EngineIni::ReadBool(pINI, section, "ROFCurve", false);
	config.StartSet = pINI->Exists(section, "ROFCurve.Start");
	config.Start = config.StartSet ? EngineIni::ReadInt(pINI, section, "ROFCurve.Start", 0) : 0;
	config.EndSet = pINI->Exists(section, "ROFCurve.End");
	config.End = EngineIni::ReadInt(pINI, section, "ROFCurve.End", 0);
	config.Ramp = EngineIni::ReadInt(pINI, section, "ROFCurve.Ramp", kDefaultRamp);
	config.Power = EngineIni::ReadDouble(pINI, section, "ROFCurve.Power", 1.0);
	config.Decay = EngineIni::ReadInt(pINI, section, "ROFCurve.Decay", 0);
	config.Cycle = EngineIni::ReadBool(pINI, section, "ROFCurve.Cycle", false);

	ROFCurveFinalizeResult const corrected = FinalizeROFCurveConfig(config, pWeapon->ROF);

	if (corrected.RampCorrected)
	{
		Log::Warn("[ROFCurve] %s: ROFCurve.Ramp must be greater than 0; using the default %d frames",
			section, kDefaultRamp);
	}

	if (corrected.PowerClamped)
	{
		Log::Warn("[ROFCurve] %s: ROFCurve.Power is clamped to [0.1, 10.0] (was %.4f)", section,
			corrected.OriginalPower);
	}

	if (config.Enabled && !config.EndSet)
	{
		Log::Warn("[ROFCurve] %s: ROFCurve=yes but ROFCurve.End is missing; the weapon keeps its "
			"normal rate of fire", section);
	}
	else if (config.Enabled && config.End < 1)
	{
		Log::Warn("[ROFCurve] %s: ROFCurve.End must be at least 1; the weapon keeps its normal "
			"rate of fire", section);
	}

	if (config.IsUsable)
	{
		Log::Debug("[ROFCurve] %s: curve usable (start=%d end=%d ramp=%d power=%.3f decay=%d "
			"cycle=%d weapon rof=%d)", section, config.ResolvedStart, config.End, config.Ramp,
			config.Power, config.Decay, config.Cycle ? 1 : 0, pWeapon->ROF);
	}

	g_weaponCurves[pWeapon] = config;
}

// The 0x6FF2AA / 0x6FE4B0 glue: advance the curve and rewrite the engine's delay.
void ApplyROFCurve(TechnoClass* const pThis, WeaponTypeClass* const pWeapon, int& rearmDelay)
{
	if (!pThis || !pWeapon)
		return;

	// No curve, or a curve that cannot run: leave the engine's value alone and
	// leave every chain alone too (a weapon without a curve must not reset the
	// chains of the other weapons the techno carries).
	ROFCurveConfig const* const pConfig = FindWeaponCurve(pWeapon);

	if (!pConfig || !pConfig->IsUsable)
		return;

	// A delay below one frame is not a delay the curve can pace; scaling it would
	// turn an already unusable value into another one.
	if (rearmDelay < 1)
		return;

	TechnoDataExt* const pExt = TechnoDataExt::Fetch(pThis);

	if (!pExt)
	{
		if (g_missingExtWarned < 4)
		{
			++g_missingExtWarned;
			Log::Warn("[ROFCurve] TechnoClass 0x%08X has no TechnoDataExt; the curve is skipped",
				PtrOf(pThis));
		}
		return;
	}

	int const engineRearm = rearmDelay;
	double const ratio = StepROFCurve(pExt->ROFCurveChains, *pConfig, pWeapon, pWeapon->ROF,
		Unsorted::CurrentFrame);
	rearmDelay = ScaleRearmDelay(engineRearm, ratio);
	RecordROFCurveRearm(pExt->ROFCurveChains, pWeapon, rearmDelay);

	if (rearmDelay != engineRearm && g_applyLogs < kApplyLogLimit)
	{
		++g_applyLogs;
		Log::Info("[ROFCurve] %s: rearm %d -> %d frames (ratio %.3f)", pWeapon->ID, engineRearm,
			rearmDelay, ratio);
	}
}

void __cdecl Initialize(const FeatureContext& context)
{
	(void)context;
	Log::Info("[ROFCurve] initialized (enabled=%d, weapons=%zu)", g_enabled ? 1 : 0,
		g_weaponCurves.size());
}

const FeatureDescriptor kDescriptor{
	"rof_curve", // id: internal identifier
	"ROFCurve",  // iniKey: switch in [Features]
	"ROFCurve: weapon-level ROF curve (spin-up) with one chain per weapon",
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
} // namespace venture::features::rof_curve

// --- hooks -----------------------------------------------------------------

// TechnoClass::Fire_At: the reload delay the engine computed sits in EAX just
// before it is stored into RearmTimer.TimeLeft.  Both instructions are the
// `lea edx,[esi+0x2EC]` that takes the address of that timer, one per branch.
// ESI is the firing techno; the weapon is the function's own weapon-index
// argument (EBP is the frame pointer here), which the engine resolved with
// GetWeapon at 0x6FDD61-0x6FDD6F.
DEFINE_HOOK_AGAIN(0x6FE4B0, Venture_ROFCurve_Fire, 0x6)
DEFINE_HOOK(0x6FF2AA, Venture_ROFCurve_Fire, 0x6)
{
	if (!venture::features::rof_curve::IsEnabled())
		return 0;

	GET(TechnoClass*, pThis, ESI);

	if (!pThis)
		return 0;

	GET_BASE(int, weaponIndex, 0xC);

	WeaponStruct* const pSlot = pThis->GetWeapon(weaponIndex);
	WeaponTypeClass* const pWeapon = pSlot ? pSlot->WeaponType : nullptr;

	int const engineRearm = static_cast<int>(R->EAX());
	int rearmDelay = engineRearm;

	venture::features::rof_curve::ApplyROFCurve(pThis, pWeapon, rearmDelay);

	if (rearmDelay != engineRearm)
		R->EAX(static_cast<DWORD>(rearmDelay));

	// Replay the stolen `lea edx,[esi+0x2EC]`; the engine then writes the timer
	// fields itself, starting TimeLeft from EAX.
	return 0;
}

// WeaponTypeClass::LoadFromINI epilogue (two success return paths): the engine
// has read the weapon's section, pItem is the weapon type and the rules INI is
// the incoming argument.  Same address as the official Phobos / Ares / Kratos /
// Mix hooks (all size 5, all returning 0), so a co-loaded platform chains; this
// handler only reads the INI and fills its own table.
DEFINE_HOOK_AGAIN(0x7729C7, Venture_ROFCurve_WeaponType_INI, 0x5)
DEFINE_HOOK(0x7729B0, Venture_ROFCurve_WeaponType_INI, 0x5)
{
	if (!venture::features::rof_curve::IsEnabled())
		return 0;

	GET(WeaponTypeClass*, pItem, ESI);
	GET_STACK(CCINIClass*, pINI, 0xE4);

	if (pItem && pINI)
		venture::features::rof_curve::LoadWeaponCurveFromIni(pItem, pINI);

	return 0;
}
