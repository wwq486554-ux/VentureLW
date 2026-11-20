#pragma once

// ---------------------------------------------------------------------------
// Scatter core (P1 / R10) - the engine-free geometry and configuration.
//
// The platform's `LW.BallisticScatter.*` system does not draw any random number
// of its own: it reshapes the radius the host (the engine, or Ares when loaded)
// has already drawn for a shot and then turns the resulting circular offset into
// an area-uniform true ellipse.  Everything in this header is pure arithmetic -
// no engine type, no RNG, no global state - so the whole mapping is unit-tested
// without the game process (tests/test_scatter_math.cpp), while the hooks only
// feed it the values they read and write the result back.
//
// The pipeline of one shot:
//
//   1. normalize the host's radius draw to u in [0, 1] against the draw window
//      of the engine path that produced it (HostBounds);
//   2. `LW.BallisticScatter.Chance`: the host's own draw doubles as the coin.  A
//      uniform draw passes with probability exactly Chance, and a passing draw
//      is rescaled back to [0, 1] so the radius distribution stays uniform.  No
//      extra random draw is consumed, so the multiplayer RNG stream is exactly
//      the one the host produced;
//   3. radius = Min + (Max - Min) * u ^ Sigma.  Min/Max default to the host
//      window of that path, Sigma defaults to 0.5 (uniform in area) - the
//      platform's stated goal is an area-uniform scatter;
//   4. distance ramp `(distance / range) ^ (1 / Divergence)` on both Fire_At
//      branches (the visible one replaces the engine's own linear scaling with
//      it, the legacy one had no distance term at all); the classic
//      `BulletClass::MoveTo` branch keeps the engine's steeper linear
//      `distance / range` factor and does not stack Divergence on top of it;
//   5. the true ellipse: the final 2-D offset is expressed in the flight frame
//      and scaled by sqrt(Aspect) across the flight line and 1/sqrt(Aspect)
//      along it (area preserving).  Only the Fire_At exit has a free
//      post-processing site; the classic path has none (see the round's
//      handover manual, section 3.2), so it is radius-shaped only.
//
// The burst point (B14/6A, ShapeBurstOffset) is a different entry point: a
// ROT / Arm projectile is guided by the engine to the centre of the live target
// every frame, so its launch offset cannot survive - the burst point is displaced
// instead, and that displacement is the one place in the module that draws its
// own random numbers (two draws on the detonation frame, from the synchronised
// scenario RNG; see the section at the bottom of this header).
//
// Delta against the fork (Phobos-LWModified), all of it deliberate:
//   * zero new random draws on the three shaping paths - the fork redraws the
//     radius at the classic site and draws a coin for its Chance key; the burst
//     point draws like the fork does (it cannot reuse a host draw);
//   * Sigma defaults to 0.5 (area uniform) instead of the fork's 1.0 (radial
//     uniform, i.e. denser in the middle);
//   * Min/Max mean "the radius at the weapon's full range" on every path; the
//     fork keeps each engine path's own magnitude (its classic path doubles the
//     values because the engine draws U[0, 2 * global] there);
//   * the IncreaseByRange family is read into the configuration but not applied
//     yet (the round decides after the in-game data whether to port it).
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace venture::features::scatter
{
// --- documented defaults and limits ----------------------------------------
//
// The values are the fork's published ranges unless a comment says otherwise;
// they are normalized at read time so the runtime path only ever sees sane
// numbers.  The defaults are the platform's stated behaviour, not the fork's:
// Sigma 0.5 is "uniform in area".
inline constexpr double kSigmaDefault = 0.5;
inline constexpr double kSigmaMin = 0.5;
inline constexpr double kSigmaMax = 16.0;

inline constexpr double kAspectDefault = 1.0;
inline constexpr double kAspectMin = 0.125;
inline constexpr double kAspectMax = 8.0;

// `[General] LW.Divergence` (the global fallback of the per-projectile key) and
// the lower bound the fork enforces; there is deliberately no upper bound.
inline constexpr double kGlobalDivergenceDefault = 0.5;
inline constexpr double kDivergenceMin = 0.05;

inline constexpr double kChanceDefault = 1.0;

// One full turn, for the burst-point angle sample.
inline constexpr double kTwoPi = 6.283185307179586476925286766559;

// One cell is 256 leptons (Unsorted::LeptonsPerCell); the cap is a sanity bound
// on the tuning values, not a gameplay limit: 65535 leptons is ~256 cells, far
// past any scatter a map can use.
inline constexpr int kMaxRadius = 65535;

inline constexpr int kLeptonsPerCell = 256;

// The engine paths the shaping is applied to.  The first two are the two
// branches of the TechnoClass::Fire_At scatter block, the third is the classic
// FlakScatter + Inviso branch inside BulletClass::MoveTo.
enum class ScatterPath
{
	FireAtVisible, // Inaccurate + Arcing, FlakScatter && !Inviso  (0x6FE71C / 0x6FE73F)
	FireAtLegacy,  // Inaccurate + Arcing, !FlakScatter || Inviso  (0x6FE821)
	Classic,       // FlakScatter && Inviso                       (0x4687F8)
};

// A radius window in leptons.  For Min/Max keys it is "the radius at the
// weapon's full range"; for a host draw window it is "the draw interval" of that
// engine path.  The two coincide on every path with the defaults below.
struct ScatterBounds
{
	int Min = 0;
	int Max = 0;
};

// One projectile type's resolved configuration.  Filled once when its
// [Projectile] section is read (BulletTypeClass::LoadFromINI 0x46C41C) and only
// read afterwards, so no locking is needed.  The `*Set` flags exist because
// every value is nullable: an absent Min keeps the host lower bound.
struct ScatterConfig
{
	bool MinSet = false;
	int Min = 0; // leptons, radius at full range
	bool MaxSet = false;
	int Max = 0; // leptons, radius at full range
	double Sigma = kSigmaDefault;
	double Aspect = kAspectDefault;
	bool DivergenceSet = false;
	double Divergence = kGlobalDivergenceDefault;
	double Chance = kChanceDefault;

	// Read for the log only in this build: the round applies the segmented
	// interval after the real-machine data (the handover manual's section 2).
	bool IncreaseByRange = false;
	bool MinRangeSet = false;
	int MinRange = 0;
	bool MaxRangeSet = false;
	int MaxRange = 0;
	bool MinInMinRangeSet = false;
	int MinInMinRange = 0;
	bool MinInMaxRangeSet = false;
	int MinInMaxRange = 0;
	bool MaxInMinRangeSet = false;
	int MaxInMinRange = 0;
	bool MaxInMaxRangeSet = false;
	int MaxInMaxRange = 0;
};

// --- normalization ---------------------------------------------------------

inline double NormalizeScatterSigma(const double sigma)
{
	return std::isfinite(sigma) ? std::clamp(sigma, kSigmaMin, kSigmaMax) : kSigmaDefault;
}

inline double NormalizeScatterAspect(const double aspect)
{
	return std::isfinite(aspect) ? std::clamp(aspect, kAspectMin, kAspectMax) : kAspectDefault;
}

// A positive value is used as written above kDivergenceMin; a non-finite value
// falls back to the global default, so a malformed key keeps a sane ramp
// instead of silently becoming the flattest one.
inline double NormalizeScatterDivergence(const double divergence)
{
	return std::isfinite(divergence) ? std::max(divergence, kDivergenceMin)
									 : kGlobalDivergenceDefault;
}

inline double NormalizeScatterChance(const double chance)
{
	return std::isfinite(chance) ? std::clamp(chance, 0.0, 1.0) : kChanceDefault;
}

// Clamps one tuning value (leptons) into [0, kMaxRadius]; returns true when the
// value had to be changed, so the caller can report it once.
inline bool ClampScatterLeptons(int& leptons)
{
	if (leptons < 0)
	{
		leptons = 0;
		return true;
	}

	if (leptons > kMaxRadius)
	{
		leptons = kMaxRadius;
		return true;
	}

	return false;
}

// --- host windows ----------------------------------------------------------

// The radius window each engine path reproduces with nothing configured.  The
// three windows differ because the engine scales the draw at different points:
//
//   * visible Fire_At: RandomRanged(0, global) * distance / GetWeaponRange, so
//     at full range the radius is U[0, global];
//   * legacy Fire_At: RandomRanged(global / 2, global) with no distance term, so
//     at full range (the platform adds the ramp) it is U[global / 2, global];
//   * classic MoveTo: RandomRanged(0, 2 * global) * distance / range, so at full
//     range it is U[0, 2 * global].
inline ScatterBounds HostBounds(const ScatterPath path, const int globalBallisticScatter)
{
	switch (path)
	{
	case ScatterPath::FireAtVisible:
		return {0, globalBallisticScatter};
	case ScatterPath::FireAtLegacy:
		return {globalBallisticScatter / 2, globalBallisticScatter};
	case ScatterPath::Classic:
		return {0, 2 * globalBallisticScatter};
	}

	return {0, 0};
}

// --- draw normalization ----------------------------------------------------

// Maps a host radius draw onto [0, 1] using that path's own draw window.  A
// degenerate window (no global scatter at all) yields 0 - the engine cannot
// scatter there either.
inline double NormalizeDraw(const int raw, const ScatterBounds window)
{
	const int lo = std::min(window.Min, window.Max);
	const int hi = std::max(window.Min, window.Max);

	if (hi <= lo || raw <= lo)
		return 0.0;

	if (raw >= hi)
		return 1.0;

	return (raw - lo) / static_cast<double>(hi - lo);
}

// --- chance ----------------------------------------------------------------

struct ChanceRoll
{
	bool Passed = false;
	double Draw = 0.0; // the normalized radius draw to shape (rescaled to [0, 1])
};

// The host draw doubles as the coin: it is uniform on [0, 1], so P(u <= chance)
// is exactly `chance`.  A passing draw is divided by `chance`, which keeps it
// uniform on [0, 1] - the radius distribution of the shots that do scatter is
// therefore unchanged.  No random number is drawn, so a co-loaded host's RNG
// stream (and with it the multiplayer synchronisation) is untouched.
inline ChanceRoll ApplyChance(const double u, const double chance)
{
	const double normalized = NormalizeScatterChance(chance);

	if (normalized <= 0.0)
		return {};

	if (normalized >= 1.0)
		return {true, std::clamp(u, 0.0, 1.0)};

	if (u > normalized)
		return {};

	return {true, std::clamp(u / normalized, 0.0, 1.0)};
}

// --- radius shaping --------------------------------------------------------

inline ScatterBounds NormalizeScatterBounds(ScatterBounds bounds)
{
	if (bounds.Max < bounds.Min)
		std::swap(bounds.Min, bounds.Max);

	ClampScatterLeptons(bounds.Min);
	ClampScatterLeptons(bounds.Max);
	return bounds;
}

// radius = Min + (Max - Min) * u ^ Sigma, in leptons.
inline int RadiusFromDraw(const double u, const ScatterBounds target, const double sigma)
{
	const ScatterBounds bounds = NormalizeScatterBounds(target);
	const double draw = std::clamp(u, 0.0, 1.0);
	const double shaped = static_cast<double>(bounds.Min)
		+ static_cast<double>(bounds.Max - bounds.Min) * std::pow(draw, NormalizeScatterSigma(sigma));

	return std::clamp(static_cast<int>(std::lround(shaped)), 0, kMaxRadius);
}

// Resolved view of one path's configuration: the Min/Max pair that applies to
// this projectile (its own keys when written, the host window otherwise) and the
// normalized shaping values.
struct ResolvedScatter
{
	ScatterBounds Target;
	double Sigma = kSigmaDefault;
	double Aspect = kAspectDefault;
	double Divergence = kGlobalDivergenceDefault;
	double Chance = kChanceDefault;
};

inline ResolvedScatter ResolveScatter(const ScatterConfig& config, const ScatterPath path, const int globalBallisticScatter)
{
	const ScatterBounds host = HostBounds(path, globalBallisticScatter);

	ResolvedScatter resolved;
	resolved.Target.Min = config.MinSet ? config.Min : host.Min;
	resolved.Target.Max = config.MaxSet ? config.Max : host.Max;
	resolved.Target = NormalizeScatterBounds(resolved.Target);
	resolved.Sigma = NormalizeScatterSigma(config.Sigma);
	resolved.Aspect = NormalizeScatterAspect(config.Aspect);
	resolved.Divergence = NormalizeScatterDivergence(config.Divergence);
	resolved.Chance = NormalizeScatterChance(config.Chance);
	return resolved;
}

// Result of shaping one draw.  `Scatter == false` means this shot is not
// scattered at all (the Chance roll failed, or the path has no window); the
// caller then writes a zero radius and skips the rest of the shaping.
struct ShapedRadius
{
	bool Scatter = false;
	int Radius = 0; // leptons
};

// The two Fire_At radius sites (0x6FE71C visible / 0x6FE821 legacy): remap the
// host's draw onto the configured radius window.
inline ShapedRadius ShapeFireAtDraw(const ScatterConfig& config, const ScatterPath path,
	const int rawDraw, const int globalBallisticScatter)
{
	ShapedRadius result;

	if (globalBallisticScatter <= 0)
		return result;

	const ResolvedScatter resolved = ResolveScatter(config, path, globalBallisticScatter);
	const ChanceRoll roll = ApplyChance(NormalizeDraw(rawDraw, HostBounds(path, globalBallisticScatter)),
		resolved.Chance);

	if (!roll.Passed)
		return result;

	result.Scatter = true;
	result.Radius = RadiusFromDraw(roll.Draw, resolved.Target, resolved.Sigma);
	return result;
}

// --- distance ramp ---------------------------------------------------------

inline double FireAtDistanceFactor(const double distance, const double range, const double divergence)
{
	if (!(range > 0.0))
		return 1.0;

	const double fraction = std::clamp(distance / range, 0.0, 1.0);

	if (fraction <= 0.0)
		return 0.0;

	const double exponent = 1.0 / NormalizeScatterDivergence(divergence);
	return exponent == 1.0 ? fraction : std::pow(fraction, exponent);
}

// The classic path keeps the engine's own linear distance / range factor.
// Unlike the Fire_At ramp it is not clamped at 1: firing beyond the weapon's
// range grows the radius exactly like the unmodified engine does.
inline double ClassicDistanceFactor(const double distance, const double range)
{
	if (!(range > 0.0))
		return 0.0;

	return std::max(distance, 0.0) / range;
}

// Scales a shaped radius by a distance factor and clamps the result.  A
// non-positive or non-finite factor yields zero (the caller should not call it
// in that case, but keeping the function total avoids a surprise on the hot
// path).
inline int ScaleRadius(const int radius, const double factor)
{
	if (radius <= 0 || !std::isfinite(factor) || factor <= 0.0)
		return 0;

	const double scaled = static_cast<double>(radius) * factor;

	if (!(scaled < static_cast<double>(kMaxRadius)))
		return kMaxRadius;

	return std::clamp(static_cast<int>(std::lround(scaled)), 1, kMaxRadius);
}

// The two Fire_At branches: multiply the shaped radius by the distance ramp
// (0x6FE73F for the visible branch, 0x6FE821 for the legacy one).
inline int ApplyFireAtDistance(const int shapedRadius, const double distance, const double range,
	const double divergence)
{
	return ScaleRadius(shapedRadius, FireAtDistanceFactor(distance, range, divergence));
}

// The classic BulletClass::MoveTo site (0x4687F8): the engine has already
// stored `draw * distance / range` in EAX there, so the draw is normalized
// against that same window and the result scaled back by the same factor.
inline ShapedRadius ShapeClassicRadius(const ScatterConfig& config, const int engineRadius,
	const int globalBallisticScatter, const double distance, const double range)
{
	ShapedRadius result;

	if (globalBallisticScatter <= 0 || !(range > 0.0) || !(distance > 0.0))
		return result;

	const double window = 2.0 * static_cast<double>(globalBallisticScatter) * distance / range;

	if (!(window > 0.0))
		return result;

	const double u = std::clamp(static_cast<double>(std::max(engineRadius, 0)) / window, 0.0, 1.0);
	const ResolvedScatter resolved = ResolveScatter(config, ScatterPath::Classic, globalBallisticScatter);
	const ChanceRoll roll = ApplyChance(u, resolved.Chance);

	if (!roll.Passed)
		return result;

	result.Scatter = true;
	result.Radius = ScaleRadius(RadiusFromDraw(roll.Draw, resolved.Target, resolved.Sigma),
		ClassicDistanceFactor(distance, range));
	return result;
}

// --- the true ellipse ------------------------------------------------------

// Area-preserving ellipse around the scatter centre: the offset is expressed in
// the flight frame (along the shot, lateral) and scaled by 1/sqrt(Aspect) along
// the flight line and sqrt(Aspect) across it, so the ellipse's area equals the
// circle's.  `directionX`/`directionY` is any non-zero horizontal vector along
// the flight line (the platform passes the muzzle -> aim offset, which is the
// base the engine scatters around).  A degenerate (straight up / straight down)
// direction leaves the offset untouched - there is no flight line to align to.
inline void ApplyScatterAspect(int& offsetX, int& offsetY, const int directionX, const int directionY,
	const double aspect)
{
	const double ratio = NormalizeScatterAspect(aspect);

	if (ratio == 1.0 || (offsetX == 0 && offsetY == 0))
		return;

	const double fx = static_cast<double>(directionX);
	const double fy = static_cast<double>(directionY);
	const double length = std::sqrt(fx * fx + fy * fy);

	// A sub-lepton horizontal direction is not a usable flight line.
	if (!(length >= 1.0))
		return;

	const double ux = fx / length;
	const double uy = fy / length;

	const double along = static_cast<double>(offsetX) * ux + static_cast<double>(offsetY) * uy;
	const double lateral = -static_cast<double>(offsetX) * uy + static_cast<double>(offsetY) * ux;

	const double scale = std::sqrt(ratio);
	const double alongNew = along / scale;
	const double lateralNew = lateral * scale;

	offsetX = static_cast<int>(std::lround(alongNew * ux - lateralNew * uy));
	offsetY = static_cast<int>(std::lround(alongNew * uy + lateralNew * ux));
}

// --- the burst point (B14/6A) ----------------------------------------------
//
// A ROT / Arm projectile is steered by the engine towards the centre of the live
// target on every frame, so neither Fire_At branch can displace where it goes
// off: they only write the launch velocity and the guidance then follows the
// target straight back to its centre.  The burst point is therefore the only
// place that can be moved without touching the guidance source (the fork's
// B14/6A, kept).  It is also the only path of this module that draws random
// numbers of its own: the two samples are taken on the detonation frame from the
// synchronised scenario RNG, so both clients of a multiplayer match compute the
// same point.  A battle that uses this path therefore sees a different
// random-number stream than one that does not (documented; not a desync - both
// ends run the same draw).
//
// The recipe is the one of the shaped paths: radius = Min + (Max - Min) * u ^
// Sigma, scaled by the Divergence ramp of the distance actually flown, turned
// into a polar offset by the angle sample and expressed in the flight frame by
// the Aspect ellipse.  `drawU` and `drawTurn` are uniform on [0, 1]; the hook
// maps the two RNG draws onto them (`drawTurn` is a fraction of a full turn).
// The burst path requires Min *and* Max to be written: it has no host draw
// window to fall back on, so a projectile that writes only one of the two keeps
// the engine's burst point.

struct BurstOffset
{
	bool Scatter = false;
	bool ChanceMiss = false; // the Chance roll failed (otherwise !Scatter means a zero radius)
	int ShapedRadius = 0;    // radius from the Min/Max window, before the distance ramp
	int Radius = 0;          // the polar radius actually used (after the ramp)
	int OffsetX = 0;
	int OffsetY = 0;
};

inline BurstOffset ShapeBurstOffset(const ScatterConfig& config, const double distance,
	const double range, const double drawU, const double drawTurn, const int directionX,
	const int directionY)
{
	BurstOffset result;

	if (!config.MinSet || !config.MaxSet)
		return result;

	// No host window is involved on this path: the resolved view is built from
	// the projectile's own pair and the normalized tuning values only.
	ResolvedScatter resolved;
	resolved.Target = NormalizeScatterBounds({config.Min, config.Max});
	resolved.Sigma = NormalizeScatterSigma(config.Sigma);
	resolved.Aspect = NormalizeScatterAspect(config.Aspect);
	resolved.Divergence = NormalizeScatterDivergence(config.Divergence);
	resolved.Chance = NormalizeScatterChance(config.Chance);

	// The radius draw doubles as the Chance coin, exactly like the Fire_At paths
	// (`Chance` cannot cost an extra draw here without changing the stream twice).
	const ChanceRoll roll = ApplyChance(std::clamp(drawU, 0.0, 1.0), resolved.Chance);

	if (!roll.Passed)
	{
		result.ChanceMiss = true;
		return result;
	}

	const int shaped = RadiusFromDraw(roll.Draw, resolved.Target, resolved.Sigma);
	const int radius = ScaleRadius(shaped, FireAtDistanceFactor(distance, range, resolved.Divergence));

	if (radius < 1)
		return result;

	const double angle = std::clamp(drawTurn, 0.0, 1.0) * kTwoPi;
	int offsetX = static_cast<int>(std::lround(std::cos(angle) * static_cast<double>(radius)));
	int offsetY = static_cast<int>(std::lround(std::sin(angle) * static_cast<double>(radius)));

	ApplyScatterAspect(offsetX, offsetY, directionX, directionY, resolved.Aspect);

	result.Scatter = true;
	result.ShapedRadius = shaped;
	result.Radius = radius;
	result.OffsetX = offsetX;
	result.OffsetY = offsetY;
	return result;
}
} // namespace venture::features::scatter
