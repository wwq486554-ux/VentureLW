#pragma once

// ---------------------------------------------------------------------------
// ROF curve - the engine-independent shape and state machine.
//
// A weapon can fire faster (or slower) the longer it keeps firing.  This header
// holds everything about that curve that does not touch the engine: the
// resolved configuration, the per-weapon spin-up chain, the curve shape and the
// state machine that advances it.  The engine-facing parts (the rules-INI read
// and the two Fire_At hooks) live in ROFCurveFeature.cpp.
//
// The header is deliberately engine-free: `WeaponTypeClass` is only used as an
// opaque identity for the chains (pointer comparison), the weapon's own ROF is
// passed in by the caller, and the current frame is a parameter.  That keeps the
// whole state machine unit-testable without the game process
// (tests/test_rof_curve.cpp).
//
// Behaviour is a direct port of Phobos-LWModified (commits 47023891 / b950ac50
// for the curve, 6c54bd6f for the per-weapon chains): the chain is identified by
// the weapon type, so the weapons of one techno never touch each other's
// progress, and the curve value is returned as a ratio to the weapon's own ROF
// so every other firing-rate modifier the engine already folded into the rearm
// delay stays intact.
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

class WeaponTypeClass;

namespace venture::features::rof_curve
{
// Frames of slack a shot may be late and still count as the same burst.  Absorbs
// the frame the rearm timer needs to expire and ordinary scheduling jitter.
inline constexpr int kContinuitySlack = 3;

// Correction targets (the fork's defaults / published range).
inline constexpr int kDefaultRamp = 30;
inline constexpr double kMinPower = 0.1;
inline constexpr double kMaxPower = 10.0;

// Type-level configuration of one weapon, resolved once when its INI section has
// been read.  `Start`/`End` keep an explicit "was it written" flag because both
// are nullable: an absent Start means "the weapon's own ROF", an absent End means
// "this weapon has no curve".
struct ROFCurveConfig
{
	bool Enabled = false; // ROFCurve=       (no curve unless yes)
	bool StartSet = false; // ROFCurve.Start= present
	int Start = 0; // ROFCurve.Start= (frames)
	bool EndSet = false; // ROFCurve.End= present
	int End = 0; // ROFCurve.End=   (frames)
	int Ramp = kDefaultRamp; // ROFCurve.Ramp=  (frames to walk Start -> End)
	double Power = 1.0; // ROFCurve.Power= (easing exponent)
	int Decay = 0; // ROFCurve.Decay= (frames to fall back after a pause; 0 = at once)
	bool Cycle = false; // ROFCurve.Cycle= (restart at the top instead of holding)

	// Resolved while the INI is read; the per-shot path only reads these.
	bool IsUsable = false;
	int ResolvedStart = 1;
};

// Spin-up state of one ROFCurve weapon on one techno.  One chain per weapon: a
// techno whose ammo logic swaps weapons keeps each weapon's progress, so firing
// one weapon never resets another one's curve.
struct ROFCurveChain
{
	const WeaponTypeClass* Weapon = nullptr; // weapon whose curve this chain belongs to
	double Progress = 0.0; // position on the curve, 0 (start) .. 1 (end)
	int LastRearm = 0; // gap the previous shot of this weapon expected to the next one
	int LastFrame = 0; // frame of the previous shot of this weapon
};

// What FinalizeROFCurveConfig corrected, so the caller can report it through the
// platform log (the plan is engine-free, so it cannot log itself).
struct ROFCurveFinalizeResult
{
	bool RampCorrected = false;
	bool PowerClamped = false;
	double OriginalPower = 1.0;
};

// Applies the two corrections the fork performs right after reading the keys and
// resolves IsUsable / ResolvedStart.  `weaponRof` is the weapon's own ROF field.
inline ROFCurveFinalizeResult FinalizeROFCurveConfig(ROFCurveConfig& config, int const weaponRof)
{
	ROFCurveFinalizeResult result;

	if (config.Ramp <= 0)
	{
		config.Ramp = kDefaultRamp;
		result.RampCorrected = true;
	}

	if (config.Power < kMinPower || config.Power > kMaxPower)
	{
		result.PowerClamped = true;
		result.OriginalPower = config.Power;
		config.Power = std::clamp(config.Power, kMinPower, kMaxPower);
	}

	config.IsUsable = config.Enabled && config.EndSet && config.End >= 1;
	config.ResolvedStart = std::max(config.StartSet ? config.Start : weaponRof, 1);
	return result;
}

// The curve itself: the eased ROF at `progress` as a ratio to the weapon's own
// ROF.  Neither end has to be the slow one - a Start above End is a curve that
// starts fast and winds down.
inline double GetROFCurveRofRatio(double const progress, int const weaponRof,
	const ROFCurveConfig& config)
{
	double const ease = std::pow(std::clamp(progress, 0.0, 1.0), config.Power);
	double const curveRof = static_cast<double>(config.ResolvedStart)
		+ (static_cast<double>(config.End) - static_cast<double>(config.ResolvedStart)) * ease;

	return curveRof / static_cast<double>(std::max(weaponRof, 1));
}

inline ROFCurveChain* FindROFCurveChain(std::vector<ROFCurveChain>& chains,
	const WeaponTypeClass* const pWeapon)
{
	for (auto& chain : chains)
	{
		if (chain.Weapon == pWeapon)
			return &chain;
	}

	return nullptr;
}

inline const ROFCurveChain* FindROFCurveChain(const std::vector<ROFCurveChain>& chains,
	const WeaponTypeClass* const pWeapon)
{
	for (auto const& chain : chains)
	{
		if (chain.Weapon == pWeapon)
			return &chain;
	}

	return nullptr;
}

// Advances this weapon's chain by the time that passed since its previous shot
// and returns the curve's ROF as a ratio to the weapon's own ROF.  A weapon
// without a usable curve returns 1.0 and touches no chain at all - that is what
// keeps the other weapons of the techno independent.
inline double StepROFCurve(std::vector<ROFCurveChain>& chains, const ROFCurveConfig& config,
	const WeaponTypeClass* const pWeapon, int const weaponRof, int const now)
{
	if (!pWeapon || !config.IsUsable)
		return 1.0;

	ROFCurveChain* pChain = FindROFCurveChain(chains, pWeapon);
	bool const fresh = pChain == nullptr;

	if (fresh)
	{
		chains.emplace_back();
		pChain = &chains.back();
		pChain->Weapon = pWeapon;
	}

	// Same burst: the same weapon fired again no later than a few frames after the
	// interval it was last handed.  A different target does not break the chain.
	long long const sinceLastFrame = static_cast<long long>(now) - pChain->LastFrame;
	bool const continuing = !fresh
		&& sinceLastFrame <= static_cast<long long>(pChain->LastRearm) + kContinuitySlack;

	// The gap is the real time that passed: the spin-up follows real time when the
	// engine holds a shot back, the decay follows real idle time.
	long long const gap = fresh ? 0 : std::max(sinceLastFrame, 0LL);

	if (continuing)
	{
		pChain->Progress += static_cast<double>(gap) / static_cast<double>(config.Ramp);
	}
	else if (config.Decay > 0)
	{
		pChain->Progress -= static_cast<double>(gap) / static_cast<double>(config.Decay);
	}
	else
	{
		pChain->Progress = 0.0;
	}

	pChain->LastFrame = now;
	pChain->Progress = std::clamp(pChain->Progress, 0.0, 1.0);

	// Reaching the top either holds (Cycle=no) or starts the curve over (Cycle=yes).
	if (config.Cycle && pChain->Progress >= 1.0)
		pChain->Progress = 0.0;

	return GetROFCurveRofRatio(pChain->Progress, weaponRof, config);
}

// Records the gap this weapon's chain expects to its next shot.  No-op when the
// weapon has no chain (it never opened one with a usable curve).
inline void RecordROFCurveRearm(std::vector<ROFCurveChain>& chains,
	const WeaponTypeClass* const pWeapon, int const rearmDelay)
{
	if (ROFCurveChain* const pChain = FindROFCurveChain(chains, pWeapon))
		pChain->LastRearm = rearmDelay;
}

// Scales the engine's rearm delay by the curve ratio.  A delay below one frame is
// not a delay the curve can pace, so it stays untouched.  The result is clamped
// instead of overflowing when a very large ROFCurve.End runs into a large
// engine-computed delay.
inline int ScaleRearmDelay(int const rearmDelay, double const ratio)
{
	if (rearmDelay < 1)
		return rearmDelay;

	// The configuration cannot produce a non-positive ratio, but keep the function
	// total for a future consumer.
	if (!(ratio > 0.0))
		return rearmDelay;

	double const scaled = static_cast<double>(rearmDelay) * ratio;

	if (scaled >= static_cast<double>(std::numeric_limits<int>::max()))
		return std::numeric_limits<int>::max();

	return std::max(static_cast<int>(std::lround(scaled)), 1);
}
} // namespace venture::features::rof_curve
