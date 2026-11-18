#pragma once

// ---------------------------------------------------------------------------
// Shared platform extension for every TechnoClass object.
//
// A game object carries at most ONE platform extension
// (docs/EngineBindings.md, "Extension model"): every feature that needs
// per-techno state keeps its fields in this one class, so the instance is
// allocated by the platform's own lifecycle hooks no matter which feature
// switch is on.  A feature must NOT declare its own TechnoClass extension
// store: the store of the second claim would fail to attach to the owner.
//
// The state kept here is deliberately transient (per frame / per shot): the
// class has no Save/Load overrides and the store stays non-saveable, so a
// savegame round trip simply rebuilds it from scratch (see docs/SaveFormat.md).
//
// Current fields:
//   * TemporalWarpWeapon - TemporalAnySlot feature: the WeaponTypeClass that
//     delivered the last Temporal warhead from this techno.  The engine derives
//     the erase rate of a running warp from the techno's *current* weapon
//     selection, which is not necessarily the weapon that fired; this records
//     the real one (see src/Features/TemporalAnySlot/TemporalAnySlot.cpp).
//   * ROFCurveChains - ROFCurve feature: one spin-up chain per weapon the techno
//     has fired with a usable curve.  Transient, per weapon, deliberately not
//     serialized (see src/Features/ROFCurve/ROFCurveMath.h).
//
// The class must know the state types of the features that keep fields here (it
// is the composition point), so it includes the ROFCurve math header; that
// header stays engine-free, which keeps the feature's unit test free of engine
// headers as well.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <vector>

#include "Ext/ExtensionStore.h"
#include "Ext/ExtHierarchy.h"

#include "Features/ROFCurve/ROFCurveMath.h"

class TechnoClass;
class WeaponTypeClass;

namespace venture
{
class TechnoDataExt final : public TechnoExt
{
public:
	// First user type id (AbstractExt::FirstUserTypeId); one id for the whole
	// platform, because this is the most derived extension declared for
	// TechnoClass and every base view (TechnoExt / FootExt) reaches it.
	static constexpr std::uint32_t TypeId = 0x0100u;
	static constexpr std::uint32_t BaseTypeId = TechnoExt::TypeId;

	explicit TechnoDataExt(AbstractClass* const owner) : TechnoExt(owner) {}

	std::uint32_t ExtTypeId() const override { return TypeId; }

	// Last Temporal warhead weapon that hit through this techno (transient).
	WeaponTypeClass* TemporalWarpWeapon = nullptr;

	// ROF curve spin-up state, one chain per weapon (transient).
	std::vector<features::rof_curve::ROFCurveChain> ROFCurveChains;

	static TechnoDataExt* Fetch(const AbstractClass* const owner)
	{
		return FetchExtAs<TechnoDataExt>(owner);
	}
};

using TechnoDataStore = ExtensionStore<TechnoClass, TechnoDataExt>;

// The single TechnoClass store.  Registered from TechnoDataExt.cpp; scenario
// clear and the savegame layer see it through the extension index.
TechnoDataStore& TechnoData();

// Lifecycle helpers used by the platform hooks in TechnoDataExt.cpp.  They are
// public so a future platform-level module can reuse them; features only Fetch.
void AllocateTechnoData(TechnoClass* const pTechno);
void RemoveTechnoData(TechnoClass* const pTechno);

} // namespace venture
