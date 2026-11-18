// Unit test: ViewZoom.Levels table normalisation (B1, tactical view zoom).
//
// TEST_DEPS:
// (header-only test: everything tested here lives in
// src/Features/ViewZoom/ViewZoomLevels.h, which is engine-free on purpose.  The
// engine-facing half - the 22 hooks, the surface pair and the input mapping -
// is covered by scripts/verify_build.py (hook windows), scripts/linux/
// load_probe.sh (DLL load/exports) and the in-game check, not by this program.)

#include <cmath>
#include <cstdio>

#include "Features/ViewZoom/ViewZoomLevels.h"

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

using venture::features::view_zoom::BuildLevelTable;
using venture::features::view_zoom::kDefaultLevelCount;
using venture::features::view_zoom::kDefaultLevels;
using venture::features::view_zoom::kMaxLevels;
using venture::features::view_zoom::kNativeFactor;
using venture::features::view_zoom::kWholeMapFactor;
using venture::features::view_zoom::LevelTable;

bool HasWholeMapLevel(const LevelTable& table)
{
	for (int i = 0; i < table.Count; ++i)
	{
		if (table.WholeMapLevels[i])
			return true;
	}
	return false;
}

int CountNumericLevels(const LevelTable& table)
{
	int count = 0;
	for (int i = 0; i < table.Count; ++i)
	{
		if (!table.WholeMapLevels[i])
			++count;
	}
	return count;
}
} // namespace

int __cdecl main(void)
{
	// -- the default table --------------------------------------------------
	{
		const LevelTable table = BuildLevelTable("0.25,0.33,0.5,0.67,1.0,1.25,1.5,2.0,2.5", true);

		Check(!table.UsedDefault && table.IgnoredTokens == 0 && table.DroppedTokens == 0,
			"a valid table is used as written");
		Check(table.Count == 10 && CountNumericLevels(table) == 9,
			"nine numeric levels plus the whole-map level");
		Check(table.WholeMapLevels[table.Count - 1] && Near(table.Levels[table.Count - 1], kWholeMapFactor),
			"the whole-map level is the last entry and carries the sentinel");
		Check(Near(table.Levels[0], 0.25) && Near(table.Levels[8], 2.5),
			"the numeric levels keep their order");
	}

	// -- sorting ------------------------------------------------------------
	{
		const LevelTable table = BuildLevelTable("2.5,1.0,0.25,0.5", false);

		Check(table.Count == 4 && !HasWholeMapLevel(table),
			"WholeMap=false appends no whole-map level");
		Check(Near(table.Levels[0], 0.25) && Near(table.Levels[1], 0.5) && Near(table.Levels[2], 1.0)
				&& Near(table.Levels[3], 2.5),
			"numeric levels are sorted ascending");
	}

	// -- an explicitly written whole-map keyword is kept (fork bug fixed) ----
	{
		const LevelTable explicitFull = BuildLevelTable("0.5,1.0,full", true);
		Check(explicitFull.Count == 3 && HasWholeMapLevel(explicitFull)
				&& Near(explicitFull.Levels[2], kWholeMapFactor),
			"an explicit `full` token yields the whole-map level (fork dropped it)");

		const LevelTable keywordLast = BuildLevelTable("full,2.0,0.5,1.0", true);
		Check(keywordLast.Count == 4 && HasWholeMapLevel(keywordLast)
				&& Near(keywordLast.Levels[3], kWholeMapFactor),
			"the whole-map level always sits after the sorted numeric levels");

		const LevelTable synonyms = BuildLevelTable("0.5,1.0,WHOLEMAP", true);
		Check(synonyms.Count == 3 && HasWholeMapLevel(synonyms),
			"the whole-map keywords are ASCII case-insensitive");

		const LevelTable repeated = BuildLevelTable("0.5,1.0,full,all", true);
		Check(repeated.Count == 3 && repeated.IgnoredTokens == 1 && HasWholeMapLevel(repeated),
			"a repeated whole-map keyword is ignored, not appended twice");
	}

	// -- fallback (fewer than two numeric levels) ----------------------------
	{
		const LevelTable empty = BuildLevelTable("", true);
		Check(empty.UsedDefault && empty.Count == kDefaultLevelCount + 1,
			"an empty value falls back to the default table plus the whole-map level");
		Check(Near(empty.Levels[0], kDefaultLevels[0])
				&& Near(empty.Levels[kDefaultLevelCount - 1], kDefaultLevels[kDefaultLevelCount - 1]),
			"the fallback table is the documented default");

		const LevelTable single = BuildLevelTable("1.0", true);
		Check(single.UsedDefault && CountNumericLevels(single) == kDefaultLevelCount,
			"a single numeric level cannot be stepped: default table");

		const LevelTable twoLevels = BuildLevelTable("0.5,1.0", false);
		Check(!twoLevels.UsedDefault && twoLevels.Count == 2 && !HasWholeMapLevel(twoLevels),
			"two numeric levels are a usable table, and WholeMap=false appends nothing");

		const LevelTable invalid = BuildLevelTable("1.0,abc", true);
		Check(invalid.UsedDefault && invalid.IgnoredTokens == 1,
			"one numeric level plus one invalid token falls back and counts the token");

		const LevelTable wholeMapOnly = BuildLevelTable("full", true);
		Check(wholeMapOnly.UsedDefault && wholeMapOnly.Count == kDefaultLevelCount + 1
				&& HasWholeMapLevel(wholeMapOnly),
			"a whole-map level alone is not usable: default table plus one appended whole-map level");
	}

	// -- invalid tokens -----------------------------------------------------
	{
		const LevelTable table = BuildLevelTable("0.5,zero,1.0,0,-2,1x", true);
		Check(table.Count == 3 && table.IgnoredTokens == 4,
			"non-numeric, zero, negative and trailing-garbage tokens are ignored and counted");
		Check(Near(table.Levels[0], 0.5) && Near(table.Levels[1], 1.0) && Near(table.Levels[2], kWholeMapFactor),
			"only the two valid factors survive");

		const LevelTable spaces = BuildLevelTable(" 0.5 , 1.0 , full ", true);
		Check(spaces.Count == 3 && spaces.IgnoredTokens == 0 && HasWholeMapLevel(spaces),
			"whitespace around a token is trimmed");
	}

	// -- the 16 level limit -------------------------------------------------
	{
		// 20 numeric tokens: the first 16 are kept, the rest are counted as dropped,
		// and the whole-map level has no room (it is not appended).
		const LevelTable table = BuildLevelTable(
			"0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,1.0,1.1,1.2,1.3,1.4,1.5,1.6,1.7,1.8,1.9,2.0", true);

		Check(table.Count == kMaxLevels && CountNumericLevels(table) == kMaxLevels,
			"at most 16 numeric levels are kept");
		Check(table.DroppedTokens == 4, "tokens past the limit are counted");
		Check(!HasWholeMapLevel(table), "a full table has no room for the whole-map level");

		// 15 numeric tokens + an explicit full: 16 entries, whole-map kept.
		const LevelTable withFull = BuildLevelTable(
			"0.1,0.2,0.3,0.4,0.5,0.6,0.7,0.8,0.9,1.0,1.1,1.2,1.3,1.4,1.5,full", true);
		Check(withFull.Count == kMaxLevels && HasWholeMapLevel(withFull)
				&& Near(withFull.Levels[kMaxLevels - 1], kWholeMapFactor),
			"an explicit whole-map level fits in a 16 entry table");
	}

	// -- degenerate input ---------------------------------------------------
	{
		const LevelTable nullTable = BuildLevelTable(nullptr, false);
		Check(nullTable.UsedDefault && nullTable.Count == kDefaultLevelCount && !HasWholeMapLevel(nullTable),
			"a null value falls back to the default table");

		const LevelTable nativeOnly = BuildLevelTable("1.0,1.0", true);
		Check(nativeOnly.Count == 3 && Near(nativeOnly.Levels[0], kNativeFactor),
			"a duplicated native level is kept as written (sorting is stable enough)");

		const LevelTable huge = BuildLevelTable("1.0,1000.0", true);
		Check(huge.Count == 3 && Near(huge.Levels[1], 1000.0),
			"a very large factor is still a valid level (the renderer refuses what it cannot build)");
	}

	std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAILED", g_failures,
		g_failures == 1 ? "" : "s");
	return g_failures == 0 ? 0 : 1;
}
