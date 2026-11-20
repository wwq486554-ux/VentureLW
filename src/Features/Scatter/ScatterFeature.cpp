// ---------------------------------------------------------------------------
// Scatter core feature module (P1 / R10) - the platform glue.
//
// The module implements the platform's own `LW.BallisticScatter.*` scatter
// system (design: the workspace's `VentureLW弹道与散布体系设计.md`, a document
// outside this repository; implementation plan: the round's handover manual).
// This file owns everything that touches the engine outside the hooks:
//
//   * the projectile-type configuration table (`[Projectile]` keys read in the
//     BulletTypeClass::LoadFromINI success epilogue, 0x46C41C);
//   * the global default `[General] LW.Divergence`, read through the platform's
//     rules-INI notification when the engine has parsed rules(md).ini;
//   * the feature descriptor ([Features] Scatter, default on).
//
// The geometry itself is engine-free and lives in ScatterMath.h; the engine
// hooks live in Hooks.Scatter.cpp.
//
// Key surface of this commit (all in the projectile's own section):
//
//   LW.BallisticScatter.Min / .Max   radius at full range, in cells (x256)
//   LW.BallisticScatter.Sigma        radius exponent, default 0.5 (area uniform)
//   LW.BallisticScatter.Aspect       area-preserving ellipse ratio, default 1
//   LW.BallisticScatter.Divergence   distance ramp; absent = [General] LW.Divergence
//   LW.BallisticScatter.Chance       per-shot scatter probability, default 1
//   LW.BallisticScatter.IncreaseByRange (+ its six range keys)
//                                    read, but *not applied* in this build: a
//                                    projectile that writes it keeps the host
//                                    behaviour until the round decides on the
//                                    segmented interval (a warning says so)
//
// `LW.BallisticScatter.Physical` (B16) and the burst-point scatter (B14/6A) are
// the round's next commits and are not read here.
//
// Enabling is data-driven, with no separate switch and no conflict detection:
// a projectile section that contains any one of the keys above is shaped; a
// section that contains none keeps the host behaviour bit for bit.  The vanilla
// `BallisticScatter.*` / `Sigma` / `Aspect` / `Divergence` keys are never read
// here and keep belonging to the host - mixing them with the `LW.` keys is
// reported because it changes the draw window the platform normalizes against.
// ---------------------------------------------------------------------------

#include "Core/EngineIni.h"
#include "Core/IniTakeover.h"
#include "Core/Log.h"
#include "Engine/EngineCompat.h"

#include "Features/Scatter/Scatter.h"

#include <platform/Feature.h>

#include <cstdio>
#include <unordered_map>

namespace venture::features::scatter
{
namespace
{
// Written by the platform before Initialize() runs (FeatureContext contract).
bool g_enabled = false;

// Resolved `[General] LW.Divergence`; the per-projectile key wins over it.
double g_globalDivergence = kGlobalDivergenceDefault;

// Per-projectile-type configuration, filled once when rules(md).ini is parsed.
// Bullet type objects are process-lifetime, so the pointer keys are stable; a
// re-parse (if the engine ever does one) simply overwrites the entry.
std::unordered_map<const BulletTypeClass*, ScatterConfig> g_configs;

// Diagnostic caps: a misconfigured mod must not flood the log.
constexpr int kWarningLimit = 8;
constexpr int kConfigLogLimit = 16;
int g_tuningWarns = 0;
int g_mixWarns = 0;
int g_deferredWarns = 0;
int g_configLogs = 0;

// Any one of these makes the projectile take part in the platform's shaping.
const char* const kGateKeys[] = {
	"LW.BallisticScatter.Min",
	"LW.BallisticScatter.Max",
	"LW.BallisticScatter.Sigma",
	"LW.BallisticScatter.Aspect",
	"LW.BallisticScatter.Divergence",
	"LW.BallisticScatter.Chance",
	"LW.BallisticScatter.IncreaseByRange",
	"LW.BallisticScatter.MinRange",
	"LW.BallisticScatter.MaxRange",
	"LW.BallisticScatter.Min.InMinRange",
	"LW.BallisticScatter.Min.InMaxRange",
	"LW.BallisticScatter.Max.InMinRange",
	"LW.BallisticScatter.Max.InMaxRange",
};

// Host keys that must not be mixed with ours: Ares / official Phobos read them
// at the same sites and changing them moves the draw window the platform
// normalizes against.
const char* const kHostKeys[] = {
	"BallisticScatter.Min",
	"BallisticScatter.Max",
	"Sigma",
	"Aspect",
	"Divergence",
};

// Sections are the projectile type ids; the keys always live in the type's own
// [Projectile] section, never in a platform-owned section.
constexpr const char* kGeneralSection = "General";
constexpr const char* kGlobalDivergenceKey = "LW.Divergence";

const char* const kKeyMin = "LW.BallisticScatter.Min";
const char* const kKeyMax = "LW.BallisticScatter.Max";
const char* const kKeySigma = "LW.BallisticScatter.Sigma";
const char* const kKeyAspect = "LW.BallisticScatter.Aspect";
const char* const kKeyDivergence = "LW.BallisticScatter.Divergence";
const char* const kKeyChance = "LW.BallisticScatter.Chance";
const char* const kKeyIncreaseByRange = "LW.BallisticScatter.IncreaseByRange";
const char* const kKeyMinRange = "LW.BallisticScatter.MinRange";
const char* const kKeyMaxRange = "LW.BallisticScatter.MaxRange";
const char* const kKeyMinInMinRange = "LW.BallisticScatter.Min.InMinRange";
const char* const kKeyMinInMaxRange = "LW.BallisticScatter.Min.InMaxRange";
const char* const kKeyMaxInMinRange = "LW.BallisticScatter.Max.InMinRange";
const char* const kKeyMaxInMaxRange = "LW.BallisticScatter.Max.InMaxRange";

// Reads one cells-valued key and converts it to leptons the way the fork's
// Leptons type does (ReadDouble * 256, rounded).  Returns false when the key is
// absent; a malformed or out-of-range value is reported and clamped.
bool ReadLeptonsKey(CCINIClass* const pINI, const char* const section, const char* const key, int& value)
{
	if (!pINI->Exists(section, key))
		return false;

	const double cells = EngineIni::ReadDouble(pINI, section, key, 0.0);

	if (!std::isfinite(cells) || cells < 0.0 || cells * kLeptonsPerCell > static_cast<double>(kMaxRadius))
	{
		if (g_tuningWarns < kWarningLimit)
		{
			++g_tuningWarns;
			Log::Warn("[Scatter] %s: %s=%g must be a non-negative number of cells within 0..%d; "
					  "the value is clamped",
				section, key, cells, kMaxRadius / kLeptonsPerCell);
		}
	}

	const double leptons = std::clamp(cells * static_cast<double>(kLeptonsPerCell), 0.0,
		static_cast<double>(kMaxRadius));
	value = static_cast<int>(std::lround(leptons));
	return true;
}

// Reads one floating point key, normalizes it with `normalize` and reports a
// value that had to be corrected.
double ReadTuningDouble(CCINIClass* const pINI, const char* const section, const char* const key,
	const double defaultValue, double (*normalize)(double), const char* const expected)
{
	const double raw = EngineIni::ReadDouble(pINI, section, key, defaultValue);
	const double normalized = normalize(raw);

	if (normalized != raw && g_tuningWarns < kWarningLimit)
	{
		++g_tuningWarns;
		Log::Warn("[Scatter] %s: %s=%g is out of range (%s); using %g", section, key, raw, expected,
			normalized);
	}

	return normalized;
}

// The old keys and the platform keys cannot be mixed: the host key moves exactly
// the draw window the platform normalizes against (Ares / official Phobos read
// those keys at the very same sites), so shaping with them would be wrong.  The
// caller therefore drops the whole platform configuration for that projectile
// and leaves it to the host - a predictable outcome, unlike shaping against a
// window that no longer matches the draw.  Returns true when a host key was
// found.
bool HasHostKeys(CCINIClass* const pINI, const char* const section)
{
	for (const char* const key : kHostKeys)
	{
		if (!pINI->Exists(section, key))
			continue;

		if (g_mixWarns < kWarningLimit)
		{
			++g_mixWarns;
			Log::Warn("[Scatter] %s: writes the host key '%s' next to LW.BallisticScatter.*; the two "
					  "families must not be mixed (the host key changes the draw window this "
					  "platform normalizes against), so this projectile keeps the host scatter - "
					  "remove the old key",
				section, key);
		}

		return true;
	}

	return false;
}

void LogConfig(const char* const section, const ScatterConfig& config)
{
	char minText[16] = {};
	char maxText[16] = {};

	if (config.MinSet)
		std::snprintf(minText, sizeof(minText), "%d", config.Min);
	else
		std::snprintf(minText, sizeof(minText), "host");

	if (config.MaxSet)
		std::snprintf(maxText, sizeof(maxText), "%d", config.Max);
	else
		std::snprintf(maxText, sizeof(maxText), "host");

	const bool infoLevel = g_configLogs < kConfigLogLimit;

	if (infoLevel)
		++g_configLogs;

	if (infoLevel)
	{
		Log::Info("[Scatter] %s: LW.BallisticScatter.* active (min=%s max=%s leptons at full range, "
				  "sigma=%.3f, aspect=%.3f, divergence=%.3f, chance=%.3f, increaseByRange=%d)",
			section, minText, maxText, config.Sigma, config.Aspect, config.Divergence, config.Chance,
			config.IncreaseByRange ? 1 : 0);
	}
	else
	{
		Log::Debug("[Scatter] %s: LW.BallisticScatter.* active (min=%s max=%s leptons at full range, "
				   "sigma=%.3f, aspect=%.3f, divergence=%.3f, chance=%.3f, increaseByRange=%d)",
			section, minText, maxText, config.Sigma, config.Aspect, config.Divergence, config.Chance,
			config.IncreaseByRange ? 1 : 0);
	}
}

// The rules-INI notification: the engine has parsed rules(md).ini and is about
// to read it, so the global default is available here.  Per-projectile sections
// are read later (inside the same RulesClass::Init), which is why the fallback
// has to be resolved before them.
void OnRulesIniReady(IniTakeover::IReader& rulesIni)
{
	if (!g_enabled)
		return;

	const double raw = rulesIni.ReadDouble(kGeneralSection, kGlobalDivergenceKey, kGlobalDivergenceDefault);
	g_globalDivergence = NormalizeScatterDivergence(raw);

	if (raw != g_globalDivergence)
	{
		Log::Warn("[Scatter] [General] %s=%g is out of range (not below %g); using %g",
			kGlobalDivergenceKey, raw, kDivergenceMin, g_globalDivergence);
	}
	else
	{
		Log::Info("[Scatter] [General] %s = %.3f", kGlobalDivergenceKey, g_globalDivergence);
	}
}

void __cdecl Initialize(const FeatureContext& context)
{
	(void)context;

	if (!g_enabled)
	{
		Log::Info("[Scatter] disabled - LW.BallisticScatter.* keys are not read and the hooks return "
				  "immediately");
		return;
	}

	// The notification is the normal route (the platform starts before the engine
	// parses rules(md).ini).  A derived platform may start later; read the
	// already-parsed INI directly in that case, exactly like the Example module.
	IniTakeover::RegisterRulesIniObserver(&OnRulesIniReady);

	if (CCINIClass* const pRules = CCINIClass::INI_Rules)
	{
		const double raw = EngineIni::ReadDouble(pRules, kGeneralSection, kGlobalDivergenceKey,
			kGlobalDivergenceDefault);
		g_globalDivergence = NormalizeScatterDivergence(raw);
		Log::Info("[Scatter] [General] %s = %.3f (read at startup: the rules INI was already parsed)",
			kGlobalDivergenceKey, g_globalDivergence);
	}

	Log::Info("[Scatter] initialized (feature switch=1, global divergence=%.3f); a projectile that "
			  "writes LW.BallisticScatter.* is shaped, a projectile without the keys keeps the host "
			  "behaviour",
		g_globalDivergence);
}

const FeatureDescriptor kDescriptor{
	"scatter", // id: internal identifier
	"Scatter", // iniKey: switch in [Features]
	"Scatter: LW.BallisticScatter.* universal scatter shaping (scatter core, P1/R10)",
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

const ScatterConfig* FindConfig(const BulletTypeClass* const pType)
{
	if (!g_enabled || !pType)
		return nullptr;

	const auto it = g_configs.find(pType);
	return it == g_configs.end() ? nullptr : &it->second;
}

double GlobalDivergence()
{
	return g_globalDivergence;
}

void LoadScatterConfigFromIni(BulletTypeClass* const pType, CCINIClass* const pINI)
{
	if (!g_enabled || !pType || !pINI)
		return;

	const char* const section = pType->ID;

	if (!section || !*section)
		return;

	bool anyKey = false;

	for (const char* const key : kGateKeys)
	{
		if (pINI->Exists(section, key))
		{
			anyKey = true;
			break;
		}
	}

	if (!anyKey)
	{
		g_configs.erase(pType);
		return;
	}

	ScatterConfig config;
	config.MinSet = ReadLeptonsKey(pINI, section, kKeyMin, config.Min);
	config.MaxSet = ReadLeptonsKey(pINI, section, kKeyMax, config.Max);

	if (config.MinSet && config.MaxSet && config.Min > config.Max)
	{
		if (g_tuningWarns < kWarningLimit)
		{
			++g_tuningWarns;
			Log::Warn("[Scatter] %s: %s is greater than %s; the two are swapped", section, kKeyMin,
				kKeyMax);
		}

		std::swap(config.Min, config.Max);
	}

	config.Sigma = ReadTuningDouble(pINI, section, kKeySigma, kSigmaDefault, &NormalizeScatterSigma,
		"0.5 .. 16");
	config.Aspect = ReadTuningDouble(pINI, section, kKeyAspect, kAspectDefault, &NormalizeScatterAspect,
		"0.125 .. 8");
	config.Chance = ReadTuningDouble(pINI, section, kKeyChance, kChanceDefault, &NormalizeScatterChance,
		"0 .. 1");

	config.DivergenceSet = pINI->Exists(section, kKeyDivergence);

	if (config.DivergenceSet)
	{
		config.Divergence = ReadTuningDouble(pINI, section, kKeyDivergence, g_globalDivergence,
			&NormalizeScatterDivergence, "not below 0.05");
	}
	else
	{
		config.Divergence = NormalizeScatterDivergence(g_globalDivergence);
	}

	// Read for the log only in this build: the round decides on the segmented
	// interval after the in-game data.  A projectile that writes it keeps the
	// host behaviour (the whole configuration is dropped) rather than silently
	// getting a half-implemented variant of the fork's semantics.
	config.IncreaseByRange = EngineIni::ReadBool(pINI, section, kKeyIncreaseByRange, false);

	if (config.IncreaseByRange)
	{
		if (g_deferredWarns < kWarningLimit)
		{
			++g_deferredWarns;
			Log::Warn("[Scatter] %s: %s=yes is read but not applied in this build (the segmented "
					  "interval is decided after the real-machine data); this projectile keeps the "
					  "host scatter",
				section, kKeyIncreaseByRange);
		}

		g_configs.erase(pType);
		return;
	}

	config.MinRangeSet = ReadLeptonsKey(pINI, section, kKeyMinRange, config.MinRange);
	config.MaxRangeSet = ReadLeptonsKey(pINI, section, kKeyMaxRange, config.MaxRange);
	config.MinInMinRangeSet = ReadLeptonsKey(pINI, section, kKeyMinInMinRange, config.MinInMinRange);
	config.MinInMaxRangeSet = ReadLeptonsKey(pINI, section, kKeyMinInMaxRange, config.MinInMaxRange);
	config.MaxInMinRangeSet = ReadLeptonsKey(pINI, section, kKeyMaxInMinRange, config.MaxInMinRange);
	config.MaxInMaxRangeSet = ReadLeptonsKey(pINI, section, kKeyMaxInMaxRange, config.MaxInMaxRange);

	if (HasHostKeys(pINI, section))
	{
		g_configs.erase(pType);
		return;
	}

	LogConfig(section, config);
	g_configs[pType] = config;
}

VENTURE_REGISTER_FEATURE(kDescriptor);
} // namespace venture::features::scatter
