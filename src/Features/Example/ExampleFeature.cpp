// ---------------------------------------------------------------------------
// Example feature: UnitClass counter
//
// The smallest complete feature module, meant to be copied when adding a real
// feature.  It demonstrates the feature contract:
//   * one FeatureDescriptor registered at load time (VENTURE_REGISTER_FEATURE);
//   * its own engine hooks, gated on the feature switch;
//   * a switch in <Platform>.ini: [Features] Example=yes|no (default: on) - the
//     INI key is descriptor.iniKey, the internal id stays example_unit_counter;
//   * sampling-based logging (see docs/Logging.md).
//
// It has no gameplay effect: it only counts UnitClass objects and logs the
// transitions, which makes it a good smoke test for the base.
//
// Platform note (VentureLW, R2): this module used to also demonstrate a
// feature-owned extension store (ExampleUnitExt : UnitExt).  A game object can
// carry only ONE platform extension (docs/EngineBindings.md, "Extension model"),
// and VentureLW's real per-techno features keep their state in the shared
// TechnoDataExt (src/Ext/TechnoDataExt.h) that extends every techno - including
// units.  Two competing stores for the same owner would make the second
// allocation fail, so the example dropped its UnitClass store and now counts via
// its ctor/dtor hooks only.  The extension-store machinery itself (shared
// TechnoDataExt, savegame trailer, scenario clear) is still exercised by the
// platform and covered by the engine-free test suite.
// ---------------------------------------------------------------------------

#include "Features/Example/ExampleFeature.h"

#include "Core/Log.h"
#include "Core/LogPolicy.h"
#include "Engine/EngineCompat.h"

#include <platform/Feature.h>

namespace venture::features::example
{
namespace
{
// Written by the platform before Initialize() runs (see FeatureContext).
bool g_enabled = false;

int g_created = 0;
int g_destroyed = 0;

// Per-unit transitions are DEBUG lines; every kProgressLogEvery-th unit also
// gets one INFO progress line, so a default (info) log still shows the smoke
// test result without two lines per unit in a big battle.  See docs/Logging.md;
// ShouldLogSample() treats 0 as "no sampling" (log every unit).
constexpr unsigned kProgressLogEvery = 100;

unsigned PtrOf(const void* const pointer)
{
	return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(pointer));
}

void __cdecl Initialize(const FeatureContext& context)
{
	(void)context;
	Log::Info("[Example] initialized (enabled=%d, created=%d, destroyed=%d)", g_enabled ? 1 : 0,
		g_created, g_destroyed);
}
} // namespace

void OnUnitCreated(UnitClass* const pUnit)
{
	if (!g_enabled || !pUnit)
		return;

	++g_created;

	// The per-unit transition is a debug detail; the sampled INFO line keeps the
	// smoke test result visible at the default level.
	Log::Debug("[Example] UnitClass 0x%08X created (created=%d)", PtrOf(pUnit), g_created);

	if (ShouldLogSample(static_cast<unsigned>(g_created), kProgressLogEvery))
		Log::Info("[Example] progress: %d created", g_created);
}

void OnUnitDestroyed(UnitClass* const pUnit)
{
	if (!pUnit)
		return;

	// Count even when the feature is currently switched off: the unit may have
	// been created while the feature was on.
	++g_destroyed;

	Log::Debug("[Example] UnitClass 0x%08X destroyed (destroyed=%d)", PtrOf(pUnit), g_destroyed);

	if (ShouldLogSample(static_cast<unsigned>(g_destroyed), kProgressLogEvery))
		Log::Info("[Example] progress: %d destroyed", g_destroyed);
}
} // namespace venture::features::example

// --- feature registration --------------------------------------------------
namespace venture::features::example
{
namespace
{
const FeatureDescriptor kDescriptor{
	"example_unit_counter", // id: internal identifier
	"Example",              // iniKey: switch in [Features]
	"Example: UnitClass counter",
	0,                      // requiredHosts (AND): no host dependency
	0,                      // anyOfHosts (OR): no host dependency
	&g_enabled,
	&Initialize,
};
} // namespace

VENTURE_REGISTER_FEATURE(kDescriptor);
} // namespace venture::features::example

// --- hooks -----------------------------------------------------------------
// UnitClass ctor: count the unit.
DEFINE_HOOK(0x7353D3, Venture_Example_UnitClass_CTOR, 0x7)
{
	GET(UnitClass*, pItem, ESI);
	venture::features::example::OnUnitCreated(pItem);
	return 0;
}

// UnitClass dtor (two call sites): count the unit.
DEFINE_HOOK_AGAIN(0x7359DA, Venture_Example_UnitClass_DTOR, 0x9)
DEFINE_HOOK(0x735967, Venture_Example_UnitClass_DTOR, 0x9)
{
	GET(UnitClass*, pItem, ESI);
	venture::features::example::OnUnitDestroyed(pItem);
	return 0;
}
