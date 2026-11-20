// Unit test: the scatter core's engine-free mapping.
//
// TEST_DEPS:
// (header-only test: everything tested here lives in
// src/Features/Scatter/ScatterMath.h, which is engine-free on purpose - the
// engine hooks are covered by scripts/verify_build.py (hook windows),
// scripts/linux/load_probe.sh (DLL load/exports) and the in-game check, and the
// INI reading is covered by the in-game log lines, not by this program.)
//
// The checks pin the documented behaviour:
//   * the three engine draw windows (visible / legacy / classic);
//   * normalization, the Sigma exponent and the area-uniform CDF (Sigma 0.5);
//   * Min/Max resolution against the per-path host default;
//   * the Chance coin (probability exactly Chance, radius distribution kept
//     uniform, no second random draw);
//   * the Divergence ramp and the classic path's engine-linear ramp;
//   * the area-preserving ellipse (long/short axes, area, rotation);
//   * bit-for-bit reproduction of the engine radius when Sigma = 1 and no
//     Min/Max are configured.

#include <cmath>
#include <cstdio>

#include "Features/Scatter/ScatterMath.h"

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

// The fraction of the radius window a draw maps to, i.e. what Sigma shapes.
double RadiusFraction(const venture::features::scatter::ScatterBounds bounds, const int radius)
{
	return (radius - bounds.Min) / static_cast<double>(bounds.Max - bounds.Min);
}

using namespace venture::features::scatter;
} // namespace

int __cdecl main()
{
	// -- the three engine draw windows --------------------------------------
	{
		const ScatterBounds visible = HostBounds(ScatterPath::FireAtVisible, 256);
		Check(visible.Min == 0 && visible.Max == 256,
			"visible Fire_At window is [0, global] at full range");

		const ScatterBounds legacy = HostBounds(ScatterPath::FireAtLegacy, 256);
		Check(legacy.Min == 128 && legacy.Max == 256,
			"legacy Fire_At window is [global/2, global] at full range");

		const ScatterBounds classic = HostBounds(ScatterPath::Classic, 256);
		Check(classic.Min == 0 && classic.Max == 512,
			"classic MoveTo window is [0, 2*global] at full range");
	}

	// -- draw normalization -------------------------------------------------
	{
		const ScatterBounds window{100, 300};

		Check(Near(NormalizeDraw(50, window), 0.0), "a draw below the window normalizes to 0");
		Check(Near(NormalizeDraw(100, window), 0.0), "a draw on the lower bound normalizes to 0");
		Check(Near(NormalizeDraw(200, window), 0.5), "the window midpoint normalizes to 0.5");
		Check(Near(NormalizeDraw(300, window), 1.0), "a draw on the upper bound normalizes to 1");
		Check(Near(NormalizeDraw(500, window), 1.0), "a draw above the window normalizes to 1");

		const ScatterBounds degenerate{0, 0};
		Check(Near(NormalizeDraw(0, degenerate), 0.0), "a degenerate window normalizes to 0");
	}

	// -- Sigma: the radius exponent and the area-uniform CDF ----------------
	{
		const ScatterBounds bounds{0, 1024};

		Check(RadiusFromDraw(0.0, bounds, 0.5) == 0, "u=0 with Sigma=0.5 maps to the lower bound");
		Check(RadiusFromDraw(1.0, bounds, 0.5) == 1024, "u=1 with Sigma=0.5 maps to the upper bound");

		// r(u) = Max * u^Sigma.  Sigma = 0.5 (the default) is uniform in area:
		// its CDF is P(r <= x * Max) = x^2, so 25% of the radius holds 6.25% of
		// the shots, 50% holds 25% and 75% holds 56.25% - i.e. the quarter draw
		// (u = 0.0625) already lands on 25% of the radius.
		Check(Near(RadiusFraction(bounds, RadiusFromDraw(0.0625, bounds, 0.5)), 0.25, 1e-6),
			"Sigma=0.5 maps the 6.25% draw to 25% of the radius (area uniform)");
		Check(Near(RadiusFraction(bounds, RadiusFromDraw(0.25, bounds, 0.5)), 0.5, 1e-6),
			"Sigma=0.5 puts the 25% draw at half the radius (area uniform)");
		Check(Near(RadiusFraction(bounds, RadiusFromDraw(0.5625, bounds, 0.5)), 0.75, 1e-6),
			"Sigma=0.5 puts the 56.25% draw at three quarters of the radius");

		// Sigma = 1.0 reproduces the engine's radial-uniform draw.
		Check(Near(RadiusFraction(bounds, RadiusFromDraw(0.25, bounds, 1.0)), 0.25, 1e-6),
			"Sigma=1.0 is the engine's radial-uniform draw");
		Check(Near(RadiusFraction(bounds, RadiusFromDraw(0.75, bounds, 1.0)), 0.75, 1e-6),
			"Sigma=1.0 stays linear at the three-quarter draw");

		// Sigma values outside the documented range are clamped, not accepted.
		Check(Near(NormalizeScatterSigma(0.0), kSigmaMin), "a Sigma below 0.5 is clamped");
		Check(Near(NormalizeScatterSigma(100.0), kSigmaMax), "a Sigma above 16 is clamped");
		Check(Near(NormalizeScatterSigma(std::nan("")), kSigmaDefault),
			"a non-finite Sigma falls back to the default");
	}

	// -- Min/Max resolution against the per-path host default ---------------
	{
		ScatterConfig config;
		ResolvedScatter resolved = ResolveScatter(config, ScatterPath::FireAtVisible, 256);
		Check(resolved.Target.Min == 0 && resolved.Target.Max == 256,
			"an unset Min/Max keeps the visible host window");
		Check(Near(resolved.Sigma, 0.5), "an unset Sigma defaults to 0.5 (area uniform)");
		Check(Near(resolved.Aspect, 1.0), "an unset Aspect keeps the circle");
		Check(Near(resolved.Chance, 1.0), "an unset Chance always scatters");

		resolved = ResolveScatter(config, ScatterPath::FireAtLegacy, 256);
		Check(resolved.Target.Min == 128 && resolved.Target.Max == 256,
			"an unset Min/Max keeps the legacy host window");
		Check(Near(resolved.Divergence, kGlobalDivergenceDefault),
			"an unset Divergence uses the global default");

		config.MinSet = true;
		config.Min = 512;
		config.MaxSet = true;
		config.Max = 1024;
		resolved = ResolveScatter(config, ScatterPath::FireAtVisible, 256);
		Check(resolved.Target.Min == 512 && resolved.Target.Max == 1024,
			"the projectile's own Min/Max replace the host window");

		config.Min = 1024;
		config.Max = 512;
		resolved = ResolveScatter(config, ScatterPath::FireAtVisible, 256);
		Check(resolved.Target.Min == 512 && resolved.Target.Max == 1024,
			"an inverted Min/Max pair is normalized, not trusted");

		config.Min = -10;
		config.Max = 0;
		resolved = ResolveScatter(config, ScatterPath::FireAtVisible, 256);
		Check(resolved.Target.Min == 0, "a negative Min is clamped to 0");
	}

	// -- Chance: probability exactly Chance, radius kept uniform -------------
	{
		ChanceRoll roll = ApplyChance(0.1, 0.5);
		Check(roll.Passed && Near(roll.Draw, 0.2), "a passing draw is rescaled by 1/Chance");

		roll = ApplyChance(0.5, 0.5);
		Check(roll.Passed && Near(roll.Draw, 1.0), "a draw on the threshold passes at full scale");

		roll = ApplyChance(0.6, 0.5);
		Check(!roll.Passed, "a draw above the threshold fails the chance roll");

		roll = ApplyChance(0.0, 0.0);
		Check(!roll.Passed, "Chance=0 never scatters");

		roll = ApplyChance(0.0, 1.0);
		Check(roll.Passed && Near(roll.Draw, 0.0), "Chance=1 always scatters at full scale");

		roll = ApplyChance(0.123, 4.0);
		Check(roll.Passed && Near(roll.Draw, 0.123), "Chance above 1 is clamped to 1");

		roll = ApplyChance(0.25, std::nan(""));
		Check(roll.Passed && Near(roll.Draw, 0.25), "a non-finite Chance falls back to always");
	}

	// -- Divergence ramp (Fire_At branches) ---------------------------------
	{
		Check(Near(FireAtDistanceFactor(640, 1280, 1.0), 0.5),
			"Divergence=1 is the engine's linear ramp");
		Check(Near(FireAtDistanceFactor(640, 1280, 0.5), 0.25),
			"Divergence=0.5 is the squared ramp (near shots scatter less)");
		Check(Near(FireAtDistanceFactor(640, 1280, 2.0), std::sqrt(0.5)),
			"Divergence=2 is the square-root ramp (near shots scatter more)");
		Check(Near(FireAtDistanceFactor(1280, 1280, 3.0), 1.0),
			"the ramp is exactly 1 at the weapon's full range");
		Check(Near(FireAtDistanceFactor(2000, 1280, 3.0), 1.0),
			"the ramp is clamped at 1 beyond the weapon's range");
		Check(Near(FireAtDistanceFactor(0, 1280, 0.5), 0.0), "the ramp is 0 at zero distance");
		Check(Near(FireAtDistanceFactor(640, 0, 0.5), 1.0),
			"an unresolved range leaves the radius unscaled");

		Check(ApplyFireAtDistance(1024, 1280, 1280, 1.0) == 1024,
			"at full range the shaped radius is passed through");
		Check(ApplyFireAtDistance(1024, 640, 1280, 0.5) == 256,
			"the shaped radius is multiplied by the squared ramp");
		Check(ApplyFireAtDistance(0, 640, 1280, 0.5) == 0, "a zero radius stays zero");
		Check(ApplyFireAtDistance(1024, 200000, 1280, 3.0) == 1024,
			"an over-range shot never exceeds the shaped radius");
	}

	// -- the two Fire_At draws, end to end ----------------------------------
	{
		ScatterConfig config;
		config.MinSet = true;
		config.Min = 512;
		config.MaxSet = true;
		config.Max = 1024;
		config.Sigma = 1.0;
		config.Divergence = 1.0;

		// Visible: raw 128 of global 256 is the middle of the draw window.
		ShapedRadius shaped = ShapeFireAtDraw(config, ScatterPath::FireAtVisible, 128, 256);
		Check(shaped.Scatter && shaped.Radius == 768,
			"the visible draw is remapped onto the configured radius window");
		Check(ApplyFireAtDistance(shaped.Radius, 1280, 1280, 1.0) == 768,
			"at full range the visible shot lands on the shaped radius");

		// Legacy: the window starts at global/2, so the same offset is the top.
		shaped = ShapeFireAtDraw(config, ScatterPath::FireAtLegacy, 256, 256);
		Check(shaped.Scatter && shaped.Radius == 1024,
			"the legacy draw is normalized against [global/2, global]");

		// Chance=0 removes the scatter instead of shaping it.
		config.Chance = 0.0;
		shaped = ShapeFireAtDraw(config, ScatterPath::FireAtVisible, 128, 256);
		Check(!shaped.Scatter, "Chance=0 yields a non-scattered shot");

		// A zero global radius has no draw window at all.
		config.Chance = 1.0;
		shaped = ShapeFireAtDraw(config, ScatterPath::FireAtVisible, 128, 0);
		Check(!shaped.Scatter, "a zero global BallisticScatter has no draw window");
	}

	// -- the classic path reproduces the engine radius when unconfigured ----
	{
		ScatterConfig config;
		config.Sigma = 1.0; // explicit: the platform default is 0.5

		// global 256, distance 1000, range 1280 => the engine's window is
		// 2 * 256 * 1000 / 1280 = 400; a draw of 200 is its middle, so the
		// remap yields 256 and the distance scaling yields the same 200 again.
		const ShapedRadius shaped = ShapeClassicRadius(config, 200, 256, 1000.0, 1280.0);
		Check(shaped.Scatter && shaped.Radius == 200,
			"Sigma=1 with no Min/Max reproduces the engine's classic radius");

		// Sigma=0.5 is uniform in area, which pushes the same shot outwards: the
		// median radius of an area-uniform disc is 0.707 * R, not 0.5 * R.
		config.Sigma = 0.5;
		const ShapedRadius uniform = ShapeClassicRadius(config, 200, 256, 1000.0, 1280.0);
		Check(uniform.Scatter && uniform.Radius > 200,
			"Sigma=0.5 pushes the classic radius outwards (area uniform)");

		Check(!ShapeClassicRadius(config, 200, 0, 1000.0, 1280.0).Scatter,
			"a zero global BallisticScatter disables the classic shaping");
		Check(!ShapeClassicRadius(config, 200, 256, 1000.0, 0.0).Scatter,
			"an unresolved weapon range disables the classic shaping");
		Check(!ShapeClassicRadius(config, 200, 256, 0.0, 1280.0).Scatter,
			"a zero distance disables the classic shaping");
	}

	// -- the area-preserving ellipse ----------------------------------------
	{
		int x = 0;
		int y = 10; // ten leptons across the flight line
		ApplyScatterAspect(x, y, 1, 0, 4.0);
		Check(x == 0 && y == 20, "Aspect=4 doubles the lateral offset");

		x = 10; // ten leptons along the flight line
		y = 0;
		ApplyScatterAspect(x, y, 1, 0, 4.0);
		Check(x == 5 && y == 0, "Aspect=4 halves the along-flight offset");

		// The transformation is area preserving: the basis vectors' image
		// rectangle has the same area as the original unit square.
		int alongX = 100;
		int alongY = 0;
		int lateralX = 0;
		int lateralY = 100;
		ApplyScatterAspect(alongX, alongY, 1, 0, 4.0);
		ApplyScatterAspect(lateralX, lateralY, 1, 0, 4.0);
		Check(Near(static_cast<double>(alongX) * lateralY - static_cast<double>(alongY) * lateralX,
				100.0 * 100.0, 1e-6),
			"the ellipse keeps the area (long axis up, short axis down)");

		// A different shot direction rotates the ellipse with it.
		x = 10;
		y = 0;
		ApplyScatterAspect(x, y, 0, 1, 4.0);
		Check(x == 20 && y == 0, "the ellipse follows the shot direction");

		// Aspect = 1 and a degenerate direction are no-ops.
		x = 7;
		y = -3;
		ApplyScatterAspect(x, y, 1, 0, 1.0);
		Check(x == 7 && y == -3, "Aspect=1 leaves the offset untouched");

		ApplyScatterAspect(x, y, 0, 0, 4.0);
		Check(x == 7 && y == -3, "a vertical shot (no horizontal direction) is not rotated");

		// A zero offset is never moved (it is the scatter centre).
		x = 0;
		y = 0;
		ApplyScatterAspect(x, y, 1, 0, 8.0);
		Check(x == 0 && y == 0, "the scatter centre is fixed");
	}

	// -- the burst point (B14/6A) -------------------------------------------
	{
		ScatterConfig config;
		config.MinSet = true;
		config.Min = 512;
		config.MaxSet = true;
		config.Max = 1024;
		config.Sigma = 1.0;
		config.Divergence = 1.0;

		// No explicit interval: the burst path has no host window to fall back
		// on, so it must not invent one.
		ScatterConfig unset;
		Check(!ShapeBurstOffset(unset, 1280.0, 1280.0, 0.5, 0.0, 1, 0).Scatter,
			"the burst path needs both Min and Max (none written)");
		unset.MinSet = true;
		unset.Min = 512;
		Check(!ShapeBurstOffset(unset, 1280.0, 1280.0, 0.5, 0.0, 1, 0).Scatter,
			"the burst path needs both Min and Max (only Min written)");
		unset.MinSet = false;
		unset.MaxSet = true;
		unset.Max = 1024;
		Check(!ShapeBurstOffset(unset, 1280.0, 1280.0, 0.5, 0.0, 1, 0).Scatter,
			"the burst path needs both Min and Max (only Max written)");

		// At full range with Sigma=1 and angle 0 the offset is straight along X.
		BurstOffset burst = ShapeBurstOffset(config, 1280.0, 1280.0, 0.5, 0.0, 1, 0);
		Check(burst.Scatter && burst.ShapedRadius == 768 && burst.Radius == 768,
			"the burst radius is Min + (Max - Min) * u ^ Sigma");
		Check(burst.OffsetX == 768 && burst.OffsetY == 0,
			"drawTurn=0 puts the burst offset on the +X axis");

		// The angle sample is a fraction of a full turn.
		burst = ShapeBurstOffset(config, 1280.0, 1280.0, 0.5, 0.25, 1, 0);
		Check(burst.OffsetX == 0 && burst.OffsetY == 768,
			"drawTurn=0.25 turns the burst offset onto the +Y axis");

		// The Divergence ramp follows the distance actually flown.
		burst = ShapeBurstOffset(config, 640.0, 1280.0, 0.5, 0.0, 1, 0);
		Check(burst.Radius == 384, "Divergence=1 halves the radius at half range");
		config.Divergence = 0.5;
		burst = ShapeBurstOffset(config, 640.0, 1280.0, 0.5, 0.0, 1, 0);
		Check(burst.Radius == 192, "Divergence=0.5 squares the half-range factor");
		config.Divergence = 1.0;

		// An unresolved weapon range leaves the shaped radius unscaled (same
		// documented fallback as the Fire_At ramp).
		burst = ShapeBurstOffset(config, 640.0, 0.0, 0.5, 0.0, 1, 0);
		Check(burst.Scatter && burst.Radius == 768,
			"an unresolved range leaves the burst radius unscaled");

		// A zero distance has no ramp and therefore no scatter.
		Check(!ShapeBurstOffset(config, 0.0, 1280.0, 0.5, 0.0, 1, 0).Scatter,
			"a zero distance disables the burst offset");

		// Sigma=0.5 pushes the same draw outwards (area uniform).
		ScatterConfig uniform = config;
		uniform.Sigma = 0.5;
		burst = ShapeBurstOffset(uniform, 1280.0, 1280.0, 0.25, 0.0, 1, 0);
		Check(burst.Radius == 768, "Sigma=0.5 maps the 25% draw to half the radius");

		// Chance uses the radius draw as the coin: a failing draw scatters
		// nothing, a passing one is rescaled to keep the distribution uniform.
		ScatterConfig lucky;
		lucky.MinSet = true;
		lucky.Min = 100;
		lucky.MaxSet = true;
		lucky.Max = 500;
		lucky.Sigma = 1.0;
		lucky.Chance = 0.5;
		Check(!ShapeBurstOffset(lucky, 1280.0, 1280.0, 0.6, 0.0, 1, 0).Scatter,
			"a burst draw above Chance does not scatter");
		burst = ShapeBurstOffset(lucky, 1280.0, 1280.0, 0.3, 0.0, 1, 0);
		Check(burst.Scatter && burst.Radius == 340,
			"a passing burst draw is rescaled by 1/Chance (0.3 -> 0.6 of the window)");

		// Min = Max is a fixed radius.
		ScatterConfig fixed = config;
		fixed.Min = 512;
		fixed.Max = 512;
		Check(ShapeBurstOffset(fixed, 1280.0, 1280.0, 0.1, 0.0, 1, 0).Radius == 512
				&& ShapeBurstOffset(fixed, 1280.0, 1280.0, 0.9, 0.0, 1, 0).Radius == 512,
			"Min=Max gives every burst the same radius");

		// The Aspect ellipse turns with the flight line, like the other paths.
		ScatterConfig oval = lucky;
		oval.Chance = 1.0;
		oval.MinSet = true;
		oval.Min = 100;
		oval.MaxSet = true;
		oval.Max = 100; // a fixed radius keeps the arithmetic readable
		oval.Aspect = 4.0;
		burst = ShapeBurstOffset(oval, 1280.0, 1280.0, 0.0, 0.0, 1, 0);
		Check(burst.OffsetX == 50 && burst.OffsetY == 0,
			"Aspect=4 halves the burst offset along the flight line");
		burst = ShapeBurstOffset(oval, 1280.0, 1280.0, 0.0, 0.25, 1, 0);
		Check(burst.OffsetX == 0 && burst.OffsetY == 200,
			"Aspect=4 doubles the burst offset across the flight line");
		burst = ShapeBurstOffset(oval, 1280.0, 1280.0, 0.0, 0.0, 0, 1);
		Check(burst.OffsetX == 200 && burst.OffsetY == 0,
			"the burst ellipse follows the flight direction");
		burst = ShapeBurstOffset(oval, 1280.0, 1280.0, 0.0, 0.0, 0, 0);
		Check(burst.OffsetX == 100 && burst.OffsetY == 0,
			"a degenerate flight direction leaves the burst offset circular");

		// Out-of-range angle samples are clamped instead of wrapping.
		burst = ShapeBurstOffset(config, 1280.0, 1280.0, 0.5, 2.0, 1, 0);
		Check(burst.OffsetX == 768 && burst.OffsetY == 0,
			"a drawTurn above 1 is clamped to a full turn");
	}

	// -- tuning value normalization -----------------------------------------
	{
		Check(Near(NormalizeScatterAspect(0.0), kAspectMin), "an Aspect below 0.125 is clamped");
		Check(Near(NormalizeScatterAspect(100.0), kAspectMax), "an Aspect above 8 is clamped");
		Check(Near(NormalizeScatterDivergence(0.0), kDivergenceMin),
			"a Divergence below 0.05 is clamped");
		Check(Near(NormalizeScatterDivergence(50.0), 50.0), "a large Divergence is used as written");
		Check(Near(NormalizeScatterChance(-1.0), 0.0), "a negative Chance is clamped to 0");
		Check(Near(NormalizeScatterChance(2.0), 1.0), "a Chance above 1 is clamped to 1");

		int leptons = -5;
		Check(ClampScatterLeptons(leptons) && leptons == 0, "a negative radius bound is clamped");
		leptons = kMaxRadius + 1;
		Check(ClampScatterLeptons(leptons) && leptons == kMaxRadius,
			"a radius bound above the sanity cap is clamped");
		leptons = 1024;
		Check(!ClampScatterLeptons(leptons) && leptons == 1024,
			"a radius bound inside the range is untouched");
	}

	if (g_failures == 0)
	{
		std::printf("PASS (0 failures)\n");
		return 0;
	}

	std::printf("FAIL %d check(s) failed\n", g_failures);
	return 1;
}
