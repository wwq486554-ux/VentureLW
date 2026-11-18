#pragma once

// ---------------------------------------------------------------------------
// ViewZoom.Levels normalisation - the engine-independent half of the tactical
// view zoom (B1, ported from Phobos-LWModified).
//
// The zoom factor k is the map render resolution relative to the resolution the
// game is set to: k < 1 magnifies (a centred crop of the native render is
// scaled up), k > 1 widens (the engine really renders a larger viewport).  The
// ini key lists the available factors; this header turns that text into the
// level table the renderer walks.
//
// Behaviour kept from the fork:
//   * comma separated factors, any order - the numeric levels are sorted
//     ascending, so "zoom in" always moves towards the lower index;
//   * at most 16 levels;
//   * a token that is not a positive number is ignored and counted;
//   * fewer than two numeric levels falls back to the documented default table
//     (a table with a single level cannot be stepped);
//   * the runtime whole-map level is a sentinel (kWholeMapFactor) at the end of
//     the table; its real factor is computed from the loaded map at run time.
//
// Deliberate fix over the fork: an explicitly written whole-map keyword
// (`full` / `whole` / `wholemap` / `all`) used to be parsed into the table and
// then silently dropped while the sorted numeric table was rebuilt - writing
// `full` actually removed the whole-map level.  The platform appends the
// whole-map level whenever `ViewZoom.WholeMap` allows it, so both an explicit
// `full` and the implicit append produce exactly one whole-map level at the end
// (and it counts towards the 16-entry limit).
//
// Engine-free on purpose: tests/test_view_zoom_levels.cpp drives this parser
// without a game process (same split as ROFCurveMath.h).
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace venture::features::view_zoom
{
inline constexpr int kMaxLevels = 16;
inline constexpr double kNativeFactor = 1.0;

// Sentinel stored in the table for the runtime whole-map level.  It has no
// fixed factor: the renderer derives it from the loaded map's pixel bounds.
inline constexpr double kWholeMapFactor = -1.0;

// Defaults measured in the fork's stage A: the 2K tactical view already burns
// 45-52% of one core, so the shipped table stops shrinking at 2.5x.
inline constexpr double kDefaultLevels[] = { 0.25, 0.33, 0.5, 0.67, 1.0, 1.25, 1.5, 2.0, 2.5 };
inline constexpr int kDefaultLevelCount = static_cast<int>(sizeof(kDefaultLevels) / sizeof(kDefaultLevels[0]));

// The resolved level table.  Levels[i] is a factor, or kWholeMapFactor for the
// runtime whole-map level; WholeMapLevels[i] says which one it is.
struct LevelTable
{
	int Count = 0;
	double Levels[kMaxLevels] = {};
	bool WholeMapLevels[kMaxLevels] = {};
	int IgnoredTokens = 0;  // tokens that are neither a positive factor nor a whole-map keyword
	int DroppedTokens = 0;  // valid tokens past the 16-entry limit
	bool UsedDefault = false; // fewer than two numeric levels: the default table applies
};

namespace detail
{
inline char LowerAscii(const char value)
{
	return (value >= 'A' && value <= 'Z') ? static_cast<char>(value + 32) : value;
}

// "full" and its documented synonyms, ASCII case-insensitive, whole token.
inline bool IsWholeMapToken(const char* const text)
{
	if (!text)
		return false;

	const char* const keywords[] = { "full", "whole", "wholemap", "all" };

	for (const char* keyword : keywords)
	{
		const char* left = text;
		const char* right = keyword;

		while (*left && LowerAscii(*left) == *right)
		{
			++left;
			++right;
		}

		if (!*left && !*right)
			return true;
	}

	return false;
}

// Trims the whitespace around one comma separated token in place.
inline void TrimToken(char* const token)
{
	if (!token)
		return;

	char* begin = token;
	while (*begin == ' ' || *begin == '\t')
		++begin;

	char* end = begin + std::strlen(begin);
	while (end > begin && (end[-1] == ' ' || end[-1] == '\t'))
		--end;

	*end = '\0';

	if (begin != token)
		std::memmove(token, begin, static_cast<std::size_t>(end - begin) + 1u);
}
} // namespace detail

// Parses a ViewZoom.Levels value.  `allowWholeMap` is the resolved
// ViewZoom.WholeMap switch: when false, the whole-map keywords are invalid
// tokens like any other non-number.  Never fails - a broken value falls back to
// the default table, exactly like the fork did.
inline LevelTable BuildLevelTable(const char* const levelsText, const bool allowWholeMap)
{
	LevelTable table{};

	if (levelsText && *levelsText)
	{
		// The platform INI registry caps one value at 511 characters; the copy is
		// still bounded so the parser is safe on its own.
		char buffer[512] = {};
		std::size_t length = std::strlen(levelsText);
		if (length >= sizeof(buffer))
			length = sizeof(buffer) - 1u;
		std::memcpy(buffer, levelsText, length);
		buffer[length] = '\0';

		char* context = nullptr;

		for (char* token = strtok_s(buffer, ",", &context); token;
			token = strtok_s(nullptr, ",", &context))
		{
			detail::TrimToken(token);

			if (table.Count >= kMaxLevels)
			{
				++table.DroppedTokens;
				continue;
			}

			if (allowWholeMap && detail::IsWholeMapToken(token))
			{
				// Only one whole-map level exists; a repeated keyword is dropped.
				bool alreadyPresent = false;
				for (int i = 0; i < table.Count; ++i)
					alreadyPresent = alreadyPresent || table.WholeMapLevels[i];

				if (!alreadyPresent)
				{
					table.Levels[table.Count] = kWholeMapFactor;
					table.WholeMapLevels[table.Count] = true;
					++table.Count;
					continue;
				}

				++table.IgnoredTokens;
				continue;
			}

			// A whole token only: the platform's typed INI reads reject trailing
			// garbage ("12abc") and the level table does the same.
			char* end = nullptr;
			const double factor = std::strtod(token, &end);
			const bool wholeToken = end != nullptr && end != token && *end == '\0';

			if (wholeToken && factor > 0.0)
			{
				table.Levels[table.Count] = factor;
				table.WholeMapLevels[table.Count] = false;
				++table.Count;
			}
			else
			{
				++table.IgnoredTokens;
			}
		}
	}

	int numericCount = 0;
	for (int i = 0; i < table.Count; ++i)
	{
		if (!table.WholeMapLevels[i])
			++numericCount;
	}

	// A single level makes stepping meaningless, and an empty table means the
	// ini is broken: fall back to the documented defaults either way.
	if (numericCount < 2)
	{
		// Keep the diagnostics: the user still wants to know that a token was
		// rejected even though the table itself falls back to the defaults.
		const int ignoredTokens = table.IgnoredTokens;
		const int droppedTokens = table.DroppedTokens;

		table = LevelTable{};
		table.UsedDefault = true;
		table.IgnoredTokens = ignoredTokens;
		table.DroppedTokens = droppedTokens;
		table.Count = kDefaultLevelCount;

		for (int i = 0; i < kDefaultLevelCount; ++i)
		{
			table.Levels[i] = kDefaultLevels[i];
			table.WholeMapLevels[i] = false;
		}
	}

	// Keep the numeric levels ascending so "zoom in" is always the lower index;
	// the whole-map level is dynamic and always sits past the numeric ones.
	double numeric[kMaxLevels] = {};
	int numericValues = 0;
	for (int i = 0; i < table.Count; ++i)
	{
		if (!table.WholeMapLevels[i])
			numeric[numericValues++] = table.Levels[i];
	}

	for (int i = 1; i < numericValues; ++i)
	{
		const double key = numeric[i];
		int j = i - 1;

		while (j >= 0 && numeric[j] > key)
		{
			numeric[j + 1] = numeric[j];
			--j;
		}

		numeric[j + 1] = key;
	}

	table.Count = 0;

	for (int i = 0; i < numericValues; ++i)
	{
		table.Levels[table.Count] = numeric[i];
		table.WholeMapLevels[table.Count] = false;
		++table.Count;
	}

	// Opt-out rather than opt-in: the whole-map level is appended unless
	// ViewZoom.WholeMap turned it off.  Unlike the fork this also holds when the
	// ini named it explicitly (see the header comment).
	if (allowWholeMap && table.Count < kMaxLevels)
	{
		table.Levels[table.Count] = kWholeMapFactor;
		table.WholeMapLevels[table.Count] = true;
		++table.Count;
	}

	return table;
}
} // namespace venture::features::view_zoom
