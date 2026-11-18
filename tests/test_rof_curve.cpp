// Unit test: ROF curve shape and per-weapon state machine.
//
// TEST_DEPS:
// (header-only test: everything tested here lives in
// src/Features/ROFCurve/ROFCurveMath.h, which is engine-free on purpose - the
// engine-facing glue and the four hooks are covered by scripts/verify_build.py
// (hook windows), scripts/linux/load_probe.sh (DLL load/exports) and the in-game
// check, not by this program.)

#include <cmath>
#include <cstdio>

#include "Features/ROFCurve/ROFCurveMath.h"

namespace
{
int g_failures = 0;

void Check(const bool ok, const char* const what)
{
	if (ok)
	{
		std::printf("ok    %s\n", what);
	}
	else
	{
		++g_failures;
		std::printf("FAIL  %s\n", what);
	}
}

bool Near(const double left, const double right, const double epsilon = 1e-9)
{
	return std::fabs(left - right) <= epsilon;
}

// The curve ROF in frames, i.e. what the game ends up applying.
int CurveRof(const double progress, const int weaponRof, const venture::features::rof_curve::ROFCurveConfig& config)
{
	return static_cast<int>(std::lround(
		venture::features::rof_curve::GetROFCurveRofRatio(progress, weaponRof, config) * weaponRof));
}

// Fake weapon identities: the chains only compare the pointer, nothing is
// dereferenced (that is what keeps this header engine-free).
const auto* const kWeaponA = reinterpret_cast<const WeaponTypeClass*>(0x1000u);
const auto* const kWeaponB = reinterpret_cast<const WeaponTypeClass*>(0x2000u);
} // namespace

int __cdecl main()
{
	using namespace venture::features::rof_curve;

	// -- finalize: minimal usable curve -------------------------------------
	{
		ROFCurveConfig config;
		config.Enabled = true;
		config.EndSet = true;
		config.End = 6;

		const auto corrected = FinalizeROFCurveConfig(config, 24);

		Check(!corrected.RampCorrected && !corrected.PowerClamped, "minimal curve needs no correction");
		Check(config.IsUsable, "ROFCurve=yes with End=6 is usable");
		Check(config.ResolvedStart == 24, "an absent ROFCurve.Start resolves to the weapon's own ROF");
		Check(config.Ramp == 30 && Near(config.Power, 1.0) && config.Decay == 0 && !config.Cycle,
			"unspecified keys keep the documented defaults");
	}

	// -- finalize: Start is nullable and clamped to >= 1 --------------------
	{
		ROFCurveConfig config;
		config.Enabled = true;
		config.EndSet = true;
		config.End = 6;
		config.StartSet = true;
		config.Start = 10;
		FinalizeROFCurveConfig(config, 24);
		Check(config.ResolvedStart == 10, "an explicit ROFCurve.Start wins over the weapon's ROF");

		config.Start = 0;
		FinalizeROFCurveConfig(config, 24);
		Check(config.ResolvedStart == 1, "ROFCurve.Start=0 is clamped to 1 frame");

		config.Start = -3;
		FinalizeROFCurveConfig(config, 24);
		Check(config.ResolvedStart == 1, "a negative ROFCurve.Start is clamped to 1 frame");
	}

	// -- finalize: not usable without a valid End ---------------------------
	{
		ROFCurveConfig config;
		config.Enabled = false;
		config.EndSet = true;
		config.End = 6;
		FinalizeROFCurveConfig(config, 24);
		Check(!config.IsUsable, "ROFCurve=no is not usable");

		config.Enabled = true;
		config.EndSet = false;
		FinalizeROFCurveConfig(config, 24);
		Check(!config.IsUsable, "a missing ROFCurve.End is not usable");

		config.EndSet = true;
		config.End = 0;
		FinalizeROFCurveConfig(config, 24);
		Check(!config.IsUsable, "ROFCurve.End=0 is not usable");

		config.End = -1;
		FinalizeROFCurveConfig(config, 24);
		Check(!config.IsUsable, "a negative ROFCurve.End is not usable");
	}

	// -- finalize: Ramp and Power corrections -------------------------------
	{
		ROFCurveConfig config;
		config.Enabled = true;
		config.EndSet = true;
		config.End = 6;

		config.Ramp = 0;
		auto corrected = FinalizeROFCurveConfig(config, 24);
		Check(corrected.RampCorrected && config.Ramp == 30, "ROFCurve.Ramp=0 falls back to 30");

		config.Ramp = -5;
		corrected = FinalizeROFCurveConfig(config, 24);
		Check(corrected.RampCorrected && config.Ramp == 30, "a negative ROFCurve.Ramp falls back to 30");

		config.Power = 0.05;
		corrected = FinalizeROFCurveConfig(config, 24);
		Check(corrected.PowerClamped && Near(config.Power, 0.1) && Near(corrected.OriginalPower, 0.05),
			"ROFCurve.Power below 0.1 is clamped to 0.1");

		config.Power = 20.0;
		corrected = FinalizeROFCurveConfig(config, 24);
		Check(corrected.PowerClamped && Near(config.Power, 10.0), "ROFCurve.Power above 10 is clamped to 10");

		config.Power = 1.5;
		corrected = FinalizeROFCurveConfig(config, 24);
		Check(!corrected.PowerClamped && Near(config.Power, 1.5), "an in-range ROFCurve.Power is untouched");
	}

	// -- curve shape: the three Power examples of the guide -----------------
	{
		ROFCurveConfig config;
		config.Enabled = true;
		config.EndSet = true;
		config.End = 6;

		config.Power = 2.0;
		FinalizeROFCurveConfig(config, 24);
		Check(CurveRof(0.0, 24, config) == 24 && CurveRof(0.25, 24, config) == 23
				&& CurveRof(0.5, 24, config) == 20 && CurveRof(0.75, 24, config) == 14
				&& CurveRof(1.0, 24, config) == 6,
			"Power=2.0: 24 23 20 14 6");

		config.Power = 1.0;
		FinalizeROFCurveConfig(config, 24);
		Check(CurveRof(0.0, 24, config) == 24 && CurveRof(0.25, 24, config) == 20
				&& CurveRof(0.5, 24, config) == 15 && CurveRof(0.75, 24, config) == 11
				&& CurveRof(1.0, 24, config) == 6,
			"Power=1.0: 24 20 15 11 6");

		config.Power = 0.5;
		FinalizeROFCurveConfig(config, 24);
		Check(CurveRof(0.0, 24, config) == 24 && CurveRof(0.25, 24, config) == 15
				&& CurveRof(0.5, 24, config) == 11 && CurveRof(0.75, 24, config) == 8
				&& CurveRof(1.0, 24, config) == 6,
			"Power=0.5: 24 15 11 8 6");
	}

	// -- reverse direction --------------------------------------------------
	{
		ROFCurveConfig config;
		config.Enabled = true;
		config.EndSet = true;
		config.StartSet = true;
		config.Start = 6;
		config.End = 30;
		FinalizeROFCurveConfig(config, 30);

		Check(CurveRof(0.0, 30, config) == 6 && CurveRof(1.0, 30, config) == 30,
			"a Start above End starts fast and winds down");
		Check(GetROFCurveRofRatio(0.5, 30, config) > GetROFCurveRofRatio(0.25, 30, config),
			"the reverse curve's ratio grows with progress");
	}

	// -- state machine: first shot opens a chain ----------------------------
	{
		ROFCurveConfig config;
		config.Enabled = true;
		config.EndSet = true;
		config.End = 6;
		FinalizeROFCurveConfig(config, 24);

		std::vector<ROFCurveChain> chains;
		const double ratio = StepROFCurve(chains, config, kWeaponA, 24, 100);

		Check(chains.size() == 1, "the first shot opens one chain");
		Check(chains[0].Weapon == kWeaponA, "the chain is keyed by the weapon");
		Check(Near(chains[0].Progress, 0.0) && chains[0].LastFrame == 100 && chains[0].LastRearm == 0,
			"a fresh chain starts at progress 0 on its shot frame");
		Check(Near(ratio, 1.0), "the first shot keeps the weapon's own ROF");
	}

	// -- state machine: continuity slack ------------------------------------
	{
		ROFCurveConfig config;
		config.Enabled = true;
		config.EndSet = true;
		config.End = 6;
		config.Ramp = 30;
		FinalizeROFCurveConfig(config, 24);

		std::vector<ROFCurveChain> chains;
		chains.push_back(ROFCurveChain { kWeaponA, 0.0, 24, 100 });

		// gap = LastRearm + slack exactly: still the same burst.
		StepROFCurve(chains, config, kWeaponA, 24, 127);
		Check(Near(chains[0].Progress, 27.0 / 30.0), "a shot within Rearm+3 frames continues the curve");

		// One frame later: no longer the same burst; Decay=0 resets at once.
		// (Rewind the chain's bookkeeping to the previous shot's frame.)
		chains[0].Progress = 0.5;
		chains[0].LastFrame = 100;
		chains[0].LastRearm = 24;
		StepROFCurve(chains, config, kWeaponA, 24, 128);
		Check(Near(chains[0].Progress, 0.0), "a shot after Rearm+3 frames is not a continuation");
	}

	// -- state machine: decay ----------------------------------------------
	{
		ROFCurveConfig config;
		config.Enabled = true;
		config.EndSet = true;
		config.End = 6;
		config.Decay = 30;
		FinalizeROFCurveConfig(config, 24);

		std::vector<ROFCurveChain> chains;
		// LastRearm=0 makes the 15-frame gap longer than the continuity allowance,
		// so the shot counts as a pause and decays.
		chains.push_back(ROFCurveChain { kWeaponA, 1.0, 0, 100 });

		// 15 idle frames fall back by 15/30 of the curve.
		StepROFCurve(chains, config, kWeaponA, 24, 115);
		Check(Near(chains[0].Progress, 0.5), "progress decays by gap/Decay after a pause");

		chains[0].Progress = 0.2;
		chains[0].LastRearm = 0;
		StepROFCurve(chains, config, kWeaponA, 24, 200);
		Check(Near(chains[0].Progress, 0.0), "decay clamps progress at 0");
	}

	// -- state machine: Cycle restarts at the top ---------------------------
	{
		ROFCurveConfig config;
		config.Enabled = true;
		config.EndSet = true;
		config.End = 6;
		config.Ramp = 30;
		config.Cycle = true;
		FinalizeROFCurveConfig(config, 24);

		std::vector<ROFCurveChain> chains;
		chains.push_back(ROFCurveChain { kWeaponA, 0.95, 30, 100 });
		StepROFCurve(chains, config, kWeaponA, 24, 110);
		Check(Near(chains[0].Progress, 0.0), "Cycle=yes restarts the curve at the top");

		config.Cycle = false;
		chains[0].Progress = 0.95;
		chains[0].LastFrame = 200;
		StepROFCurve(chains, config, kWeaponA, 24, 210);
		Check(Near(chains[0].Progress, 1.0), "Cycle=no holds the curve at the top");
	}

	// -- B18: one chain per weapon, weapons do not interfere ----------------
	{
		ROFCurveConfig curveA;
		curveA.Enabled = true;
		curveA.EndSet = true;
		curveA.End = 6;
		curveA.Ramp = 30;
		FinalizeROFCurveConfig(curveA, 24);

		ROFCurveConfig curveB;
		curveB.Enabled = true;
		curveB.EndSet = true;
		curveB.End = 12;
		curveB.Ramp = 30;
		FinalizeROFCurveConfig(curveB, 24);

		std::vector<ROFCurveChain> chains;

		// A fires twice, so its chain advances.
		StepROFCurve(chains, curveA, kWeaponA, 24, 100);
		RecordROFCurveRearm(chains, kWeaponA, 20);
		StepROFCurve(chains, curveA, kWeaponA, 24, 120);
		Check(chains.size() == 1 && chains[0].Progress > 0.0, "weapon A advances its own chain");

		const ROFCurveChain chainABefore = chains[0];

		// Backdate the chain so the next A shot continues, then fire B: B must
		// neither create progress for A nor touch A's bookkeeping.
		chains[0].LastFrame = 130;
		chains[0].LastRearm = 20;
		StepROFCurve(chains, curveB, kWeaponB, 24, 131);
		StepROFCurve(chains, curveB, kWeaponB, 24, 150);

		const ROFCurveChain* const pA = FindROFCurveChain(chains, kWeaponA);
		const ROFCurveChain* const pB = FindROFCurveChain(chains, kWeaponB);

		Check(chains.size() == 2, "a second weapon opens its own chain");
		Check(pA && pB && pA->Weapon != pB->Weapon, "the two chains are keyed by their own weapons");
		Check(pA && pA->Progress == chainABefore.Progress && pA->LastRearm == chainABefore.LastRearm,
			"firing weapon B leaves weapon A's chain untouched");

		// A weapon without a usable curve returns 1.0 and creates no chain.
		ROFCurveConfig noCurve;
		const std::size_t chainCountBefore = chains.size();
		const double ratio = StepROFCurve(chains, noCurve, kWeaponB, 24, 999);
		Check(Near(ratio, 1.0) && chains.size() == chainCountBefore,
			"a weapon without a usable curve creates no chain and returns 1.0");
	}

	// -- rearm scaling ------------------------------------------------------
	{
		Check(ScaleRearmDelay(30, 0.5) == 15, "a 0.5 ratio halves the rearm delay");
		Check(ScaleRearmDelay(30, 2.0) == 60, "a 2.0 ratio doubles the rearm delay");
		Check(ScaleRearmDelay(30, 1.0) == 30, "a 1.0 ratio keeps the delay");
		Check(ScaleRearmDelay(0, 2.0) == 0, "a zero delay is not scaled");
		Check(ScaleRearmDelay(-5, 2.0) == -5, "a negative delay is not scaled");
		Check(ScaleRearmDelay(30, 0.0) == 30, "a non-positive ratio leaves the delay alone");
		Check(ScaleRearmDelay(std::numeric_limits<int>::max() / 2, 10.0) == std::numeric_limits<int>::max(),
			"a huge scaled delay saturates instead of overflowing");
		Check(ScaleRearmDelay(1, 0.1) == 1, "the scaled delay never drops below one frame");
	}

	std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAILED", g_failures,
		g_failures == 1 ? "" : "s");
	return g_failures == 0 ? 0 : 1;
}
