// ---------------------------------------------------------------------------
// ArmedEngineers feature: an engineer that carries a real weapon can attack.
//
// Ported from Phobos-LWModified (the fork's A10 batch, commit c2341b52; its
// behaviour matrix was verified in game on 2026-09-26, see
// Waterline/已实现/一期/工程师武器攻击功能实现记录.md §4-§6).  Vanilla hardcodes
// `if (Type->Engineer && action == Action::Attack) return Action::NoMove;`
// (0x51E6B4) so an armed engineer can never get an attack cursor on a unit, and
// deeper in the general path every non-allied capturable building is rewritten
// back to Action::Capture (0x51EE91 -> 0x51EEEF) no matter which entry point was
// used - so the building case cannot be fixed from inside the function at all.
//
// Two layers, exactly like the fork:
//
//   1. 0x51E6C4 (size 5) - InfantryClass::MouseOverObject, the engineer attack
//      clamp.  Window = `cmp ebp,5` (3) + `jne 0x51E6D8` (2).  Only engineers
//      reach it (0x51E6C2 diverts every other infantry) and from 0x51E6D8 on the
//      general path has no engineer special-casing, so jumping there lets an
//      armed engineer behave like any other armed infantry.  Returning 0
//      replays the cmp/jne, so an engineer without an attack weapon keeps the
//      vanilla NoMove.
//
//   2. Vtable slot 0x7EB0CC - the real object-cursor entry point, the
//      InfantryClass::MouseOverObject slot (vanilla value 0x51E3B0), i.e. the
//      outermost layer.  The platform reads the current value at startup (after
//      every host DLL has applied its own patch, so under the usual Ares +
//      official Phobos environment this is Phobos's wrapper), replaces it with
//      the wrapper below and therefore has to *chain*: call the previous
//      implementation first and rewrite its result.  This is the platform's
//      first feature-level vtable binding; see docs/EngineBindings.md and
//      docs/Fixed-or-Improved-Logics.md.
//
//      The slot lives in the engine's read-only .rdata section
//      (0x7E1000-0x812000), so the write goes through VirtualProtect.  (The
//      fork patched it with Syringe's VTABLE feature, which the platform does
//      not use.)
//
// The override only applies when Ctrl (the force-fire key) is held, the target
// is a building, the caller did not ask to ignore force fire, the current
// result is neither Attack nor DisarmBomb and the engineer really carries an
// attack weapon - so vanilla capture/repair/enter, Ivan bomb disarm and every
// non-engineer infantry keep their behaviour.
//
// Known trade-offs (the fork's, documented there): Ctrl means attack for *any*
// building, allied or not, and the AI attack scripts still convert engineers to
// capture missions - this feature is about manual attacks only.  No INI keys,
// no savegame state; the module is `[Features] ArmedEngineers` (default on).
// ---------------------------------------------------------------------------

#include "Core/Log.h"
#include "Engine/EngineCompat.h"

#include <platform/Feature.h>

#include <BuildingClass.h>
#include <InfantryClass.h>
#include <InfantryTypeClass.h>
#include <InputManagerClass.h>

namespace venture::features::armed_engineers
{
namespace
{
// Written by the platform before Initialize() runs (see FeatureContext).
bool g_enabled = false;

// The vtable slot the platform takes over: InfantryClass::MouseOverObject
// (vanilla 0x51E3B0).  The previous value is read at startup and called first,
// so a co-loaded host wrapper keeps working.
constexpr std::uintptr_t kWhatActionSlot = 0x7EB0CCu;
constexpr std::uintptr_t kVanillaWhatAction = 0x51E3B0u;

// __fastcall: this in ECX, the dummy second parameter in EDX, the two real
// arguments on the stack - the calling shape a __thiscall virtual looks like
// from the callee side, and the one the official Phobos wrapper declares.  The
// vanilla function simply ignores EDX.
using WhatActionFn = Action(__fastcall*)(InfantryClass*, void*, ObjectClass*, bool);

WhatActionFn g_previousWhatAction = nullptr;
int g_overrideLogs = 0;

constexpr int kOverrideLogLimit = 8;

unsigned Bits(const void* const pointer)
{
	return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(pointer));
}

unsigned FunctionBits(WhatActionFn const function)
{
	return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(function));
}

// An engineer may only attack when the weapon the engine would pick for this
// target is a real attack weapon: it exists, has a warhead and is not a
// bomb-disarm tool (a DefuseKit-style weapon keeps its own cursor).
bool HasAttackWeapon(InfantryClass* const pThis, ObjectClass* const pTarget)
{
	if (!pThis || !pTarget)
		return false;

	int const index = pThis->SelectWeapon(pTarget);

	if (index < 0)
		return false;

	WeaponStruct* const pWeapon = pThis->GetWeapon(index);

	if (!pWeapon)
		return false;

	WeaponTypeClass* const pWeaponType = pWeapon->WeaponType;

	return pWeaponType != nullptr && pWeaponType->Warhead != nullptr
		&& !pWeaponType->Warhead->BombDisarm;
}

// The wrapper installed into the vtable slot: chain to the previous
// implementation (the vanilla function or a host wrapper), then rewrite the
// force-fired building case to an attack.
Action __fastcall WhatActionWrapper(InfantryClass* const pThis, void*, ObjectClass* const pObj,
	bool const ignoreForce)
{
	Action result = g_previousWhatAction
		? g_previousWhatAction(pThis, nullptr, pObj, ignoreForce)
		: Action::None;

	if (!g_enabled || !pThis || !pThis->Type || !pThis->Type->Engineer || !pObj
		|| ignoreForce)
	{
		return result;
	}

	if (result == Action::Attack || result == Action::DisarmBomb)
		return result;

	if (!abstract_cast<BuildingClass*>(pObj))
		return result;

	if (!InputManagerClass::Instance || !InputManagerClass::Instance->IsForceFireKeyPressed())
		return result;

	if (!HasAttackWeapon(pThis, pObj))
		return result;

	if (g_overrideLogs < kOverrideLogLimit)
	{
		++g_overrideLogs;
		Log::Info("[ArmedEngineers] Ctrl force-fire on a building becomes Attack (InfantryClass "
			"0x%08X, previous action %d)", Bits(pThis), static_cast<int>(result));
	}

	return Action::Attack;
}

// Writes one pointer into a read-only engine location (the vtable slot lives in
// .rdata), restoring the page protection afterwards.  Returns false when the
// protection could not be changed - the caller then simply does not install the
// wrapper.
bool WriteReadOnlyPointer(void* const address, void* const value)
{
	DWORD oldProtect = 0;

	if (!VirtualProtect(address, sizeof(void*), PAGE_READWRITE, &oldProtect))
		return false;

	*static_cast<void**>(address) = value;

	DWORD ignored = 0;
	VirtualProtect(address, sizeof(void*), oldProtect, &ignored);
	return true;
}

void __cdecl Initialize(const FeatureContext& context)
{
	(void)context;

	void* const pSlot = reinterpret_cast<void*>(kWhatActionSlot);
	g_previousWhatAction = *reinterpret_cast<WhatActionFn*>(pSlot);

	Log::Info("[ArmedEngineers] initialized (enabled=%d, vtable 0x%08X previous=0x%08X (%s))",
		g_enabled ? 1 : 0, static_cast<unsigned>(kWhatActionSlot),
		FunctionBits(g_previousWhatAction),
		reinterpret_cast<std::uintptr_t>(g_previousWhatAction) == kVanillaWhatAction
			? "vanilla" : "host wrapper");

	if (!g_enabled || !g_previousWhatAction)
		return;

	bool const installed = WriteReadOnlyPointer(pSlot, reinterpret_cast<void*>(&WhatActionWrapper));

	if (installed)
	{
		Log::Info("[ArmedEngineers] vtable slot 0x%08X chained: calls the previous "
			"implementation first, then applies the force-fire override",
			static_cast<unsigned>(kWhatActionSlot));
	}
	else
	{
		Log::Error("[ArmedEngineers] could not write vtable slot 0x%08X (VirtualProtect failed); "
			"the feature is inactive", static_cast<unsigned>(kWhatActionSlot));
	}
}

const FeatureDescriptor kDescriptor{
	"armed_engineers", // id: internal identifier
	"ArmedEngineers",  // iniKey: switch in [Features]
	"ArmedEngineers: an engineer carrying a real weapon can attack with it",
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
} // namespace venture::features::armed_engineers

// --- hooks -----------------------------------------------------------------

// InfantryClass::MouseOverObject, the vanilla engineer attack clamp:
// `cmp ebp,5` + `jne 0x51E6D8`.  Only engineers reach it, and past 0x51E6D8 the
// general path has no engineer special-casing.  EDI = this, ESI = the target,
// EBP = the base action the engine resolved.
DEFINE_HOOK(0x51E6C4, Venture_ArmedEngineers_EngineerAttackClamp, 0x5)
{
	enum { SkipEngineerAttackClamp = 0x51E6D8 };

	if (!venture::features::armed_engineers::IsEnabled())
		return 0;

	GET(InfantryClass*, pThis, EDI);
	GET(ObjectClass*, pTarget, ESI);
	GET(Action, baseAction, EBP);

	if (baseAction == Action::Attack && pThis && pThis->Owner
		&& pThis->Owner->IsControlledByCurrentPlayer()
		&& venture::features::armed_engineers::HasAttackWeapon(pThis, pTarget))
	{
		return SkipEngineerAttackClamp;
	}

	return 0;
}
