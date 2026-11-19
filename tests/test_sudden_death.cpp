// Unit test: SuddenDeath warhead key parsing.
//
// TEST_DEPS:
// (header-only test: the parser lives in
// src/Features/SuddenDeath/SuddenDeathMode.h, which is engine-free on purpose -
// the engine-facing glue and the eight hooks are covered by
// scripts/verify_build.py (hook windows and catalogue), scripts/linux/load_probe.sh
// and the in-game check, not by this program.)

#include <cstdio>

#include "Features/SuddenDeath/SuddenDeathMode.h"

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

bool ParsesTo(const char* const text, const venture::features::sudden_death::ExecutionMode expected)
{
	using namespace venture::features::sudden_death;

	ExecutionMode mode = ExecutionMode::Normal;
	return ParseExecutionMode(text, mode) && mode == expected;
}

bool Rejects(const char* const text)
{
	using namespace venture::features::sudden_death;

	ExecutionMode mode = ExecutionMode::Erase;
	return !ParseExecutionMode(text, mode) && mode == ExecutionMode::Erase;
}
} // namespace

int __cdecl main()
{
	using namespace venture::features::sudden_death;

	Check(ParsesTo("normal", ExecutionMode::Normal), "normal parses to normal");
	Check(ParsesTo("NORMAL", ExecutionMode::Normal), "NORMAL parses to normal (ASCII case insensitive)");
	Check(ParsesTo("Normal", ExecutionMode::Normal), "Normal parses to normal");
	Check(ParsesTo("erase", ExecutionMode::Erase), "erase parses to erase");
	Check(ParsesTo("ERASE", ExecutionMode::Erase), "ERASE parses to erase");
	Check(ParsesTo("Erase", ExecutionMode::Erase), "Erase parses to erase");

	Check(Rejects("1"), "a numeric value is rejected (the fork only accepts the two words)");
	Check(Rejects("0"), "zero is rejected");
	Check(Rejects(""), "an empty value is rejected");
	Check(Rejects(nullptr), "a null value is rejected");
	Check(Rejects("normal "), "a trailing space is rejected (the engine trims values, the parser is strict)");
	Check(Rejects(" normal"), "a leading space is rejected");
	Check(Rejects("normalx"), "a longer word is rejected");
	Check(Rejects("no"), "an unrelated bool value is rejected");

	if (g_failures == 0)
	{
		std::printf("PASS (0 failures)\n");
		return 0;
	}

	std::printf("FAIL %d check(s) failed\n", g_failures);
	return 1;
}
