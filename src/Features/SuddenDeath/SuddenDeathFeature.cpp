// ---------------------------------------------------------------------------
// SuddenDeath feature module (B6, R4) - the platform glue.
//
// The feature is the `[Features] SuddenDeath` switch (default on) plus the two
// warhead-section keys `SuddenDeath` and `SuddenDeath.Mode`.  There are no
// platform INI keys and no savegame state:
//
//   * the warhead keys are read from the section the engine just parsed, in the
//     WarheadTypeClass::LoadFromINI epilogue (Hooks.SuddenDeath.cpp), and kept in
//     a type-level table - warhead types are process-lifetime objects;
//   * the hit and terrain queues are transient per scenario and are dropped by
//     the platform's scenario-clear observer.
//
// Initialize() runs in both switch states (INI-05): the observer is registered
// unconditionally so a module that is switched off later still cannot inherit a
// queue from the previous scenario, and the switch only gates the behaviour.
//
// See docs/New-or-Enhanced-Logics.md and the fork's Chinese guide
// `guides/即死弹头说明.txt` (second chapter = the platform usage).
// ---------------------------------------------------------------------------

#include "Core/Log.h"
#include "Core/Platform.h"
#include "Features/SuddenDeath/SuddenDeath.h"

#include <platform/Feature.h>

namespace venture::features::sudden_death
{
namespace
{
// Written by the platform before Initialize() runs (FeatureContext contract).
bool g_enabled = false;

void __cdecl Initialize(const FeatureContext& context)
{
	(void)context;

	// Registered whether the module is on or off - Initialize() runs in both
	// states and the transient state must never survive a scenario change.
	RegisterScenarioClearObserver(&ClearPending);

	Log::Info("[SuddenDeath] initialized (feature switch=%d; %zu warhead(s) configured so far - the "
		"keys are read per warhead when rules(md).ini is parsed)",
		g_enabled ? 1 : 0, ConfiguredWarheadCount());

	Log::Info("[SuddenDeath] no platform INI keys: write SuddenDeath=yes (and optionally "
		"SuddenDeath.Mode=normal|erase) in the warhead section of rules(md).ini");

	if (!g_enabled)
	{
		Log::Info("[SuddenDeath] the module is off - hits are not recorded, the fire gate and the "
			"cursor are left to the vanilla rules");
	}
}

const FeatureDescriptor kDescriptor{
	"sudden_death", // id: internal identifier
	"SuddenDeath",  // iniKey: switch in [Features]
	"SuddenDeath: warhead-level instant kill with ore / bridge / terrain sweep",
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
} // namespace venture::features::sudden_death
