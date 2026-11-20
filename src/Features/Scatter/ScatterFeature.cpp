// ---------------------------------------------------------------------------
// Scatter core feature module (P1 / R10) - the platform glue.
//
// Scope of the work package (design document §5, original round R10):
//
//   * A1  - universal scatter shaping: the projectile's own `LW.BallisticScatter.*`
//           interval / radius exponent / ellipse ratio / distance curve applied to
//           every engine scatter path without touching the vanilla keys;
//   * true ellipse - the final offset is remapped deterministically (no extra
//           random draw) onto an area-uniform ellipse;
//   * B14(6A) - burst-point scatter for engine-guided ROT/Arm missiles;
//   * B16 - `LW.BallisticScatter.Physical=yes`: the missile really flies to the
//           scattered point;
//   * B14(6B) and the spawned-missile scatter (B15) are *not* here: the former
//           belongs to the trajectory work package (P2/R16), the latter to P3.
//
// This commit is the round's first step, exactly as the design requires (do not
// guess the host sample shape): the module registers its switch and a temporary
// `[Scatter] Probe` key, and the engine hooks in Hooks.Scatter.cpp only record
// the host's own draws/offsets at Debug level.  No gameplay logic runs yet, so a
// probe build is behaviour-identical to the previous round.
//
// The mapping and the key surface follow in the next commits of the same round;
// the probe key, the probe log lines and the diagnostic-only hooks are removed
// when the round closes.
// ---------------------------------------------------------------------------

#include "Core/Log.h"
#include "Features/Scatter/Scatter.h"

#include <platform/Feature.h>

namespace venture::features::scatter
{
namespace
{
// Written by the platform before Initialize() runs (FeatureContext contract).
bool g_enabled = false;
bool g_probe = false;
bool g_hasAres = false;

void __cdecl Initialize(const FeatureContext& context)
{
	// Declared in both switch states (INI-05): the key registration is what makes
	// the key known to the startup scan, independent of the feature switch.
	g_probe = VENTURE_INI_BOOL(context, "Scatter", "Probe", false,
		"temporary R10 diagnostic: log the raw host scatter samples instead of shaping them (read-only; removed when the round closes)");
	g_hasAres = context.HasHost && context.HasHost(Host::Ares);

	Log::Info("[Scatter] initialized (feature switch=%d, probe=%d, ares=%d)",
		g_enabled ? 1 : 0, g_probe ? 1 : 0, g_hasAres ? 1 : 0);

	// The honest state of this build: the module is only a probe so far.
	if (g_probe)
	{
		Log::Info("[Scatter] probe on: the hooks are read-only and log '[ScatterProbe]' lines at debug level; "
			"no scatter behaviour is changed in this build");
	}
	else
	{
		Log::Info("[Scatter] probe off: the hooks are installed but return immediately; this build changes no "
			"scatter behaviour (the LW.BallisticScatter.* mapping lands in the next commit of this round)");
	}
}

const FeatureDescriptor kDescriptor{
	"scatter",  // id: internal identifier
	"Scatter",  // iniKey: switch in [Features]
	"Scatter: LW.BallisticScatter.* shaping (scatter core, P1/R10; host-sample probe phase)",
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

bool IsProbeEnabled()
{
	return g_probe;
}

bool HasAres()
{
	return g_hasAres;
}

VENTURE_REGISTER_FEATURE(kDescriptor);
} // namespace venture::features::scatter
