// Unit test: the two bridge-cell tests of the engine's MapClass::DamageArea -
// the high-bridge tile-type comparison and the low-bridge overlay index ranges.
//
// TEST_DEPS:
// (header-only test: the tests live in
// src/Features/SuddenDeath/BridgeTiles.h and are engine-free on purpose; the
// engine globals they are fed with and the sweep itself are covered by
// verify_build.py / load_probe.sh / the in-game check.)

#include <cstdio>

#include "Features/SuddenDeath/BridgeTiles.h"

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

bool Match(const int tile, const int base, const int middle1, const int middle2)
{
	return venture::features::sudden_death::IsEngineBridgeTileIndex(tile, base, middle1, middle2);
}

bool LowBridge(const int overlay)
{
	return venture::features::sudden_death::IsEngineLowBridgeOverlayIndex(overlay);
}
} // namespace

int __cdecl main()
{
	// base 100, BridgeMiddle1 = 5, BridgeMiddle2 = 20: the engine's adjusted
	// value is tile - base + 1, and each group covers four consecutive slots.
	constexpr int kBase = 100;
	constexpr int kMiddle1 = 5;
	constexpr int kMiddle2 = 20;

	Check(Match(104, kBase, kMiddle1, kMiddle2), "the first slot of BridgeMiddle1 matches");
	Check(Match(107, kBase, kMiddle1, kMiddle2), "the fourth slot of BridgeMiddle1 matches");
	Check(Match(105, kBase, kMiddle1, kMiddle2), "the second slot of BridgeMiddle1 matches");
	Check(Match(119, kBase, kMiddle1, kMiddle2), "the first slot of BridgeMiddle2 matches");
	Check(Match(122, kBase, kMiddle1, kMiddle2), "the fourth slot of BridgeMiddle2 matches");

	Check(!Match(108, kBase, kMiddle1, kMiddle2), "a slot between the two groups does not match");
	Check(!Match(123, kBase, kMiddle1, kMiddle2), "a slot past BridgeMiddle2 does not match");
	Check(!Match(103, kBase, kMiddle1, kMiddle2), "a slot below BridgeMiddle1 does not match");
	Check(!Match(kBase, kBase, kMiddle1, kMiddle2), "the base tile itself does not match");
	Check(!Match(0, kBase, kMiddle1, kMiddle2), "tile index 0 does not match");

	Check(!Match(104, 0, kMiddle1, kMiddle2), "a zero base falls back to the flag-only path");
	Check(!Match(104, kBase, 0, kMiddle2), "a zero BridgeMiddle1 falls back");
	Check(!Match(104, kBase, kMiddle1, 0), "a zero BridgeMiddle2 falls back");

	Check(Match(203, 200, 1, 9), "the arithmetic is relative to the loaded base");
	Check(!Match(204, 200, 10, 1), "a group past the adjusted slot does not match");

	// The engine's low-bridge overlay ranges (MapClass::DamageArea 0x48A214 and
	// 0x48A26A): 74..99 and 205..230 inclusive, everything around them outside.
	Check(LowBridge(74), "the first slot of the asphalt low-bridge range matches");
	Check(LowBridge(99), "the last slot of the asphalt low-bridge range matches");
	Check(LowBridge(205), "the first slot of the concrete low-bridge range matches");
	Check(LowBridge(230), "the last slot of the concrete low-bridge range matches");
	Check(LowBridge(77), "the shipped-YR LOBRDG01 slot matches");

	Check(!LowBridge(73), "a slot before the asphalt range does not match");
	Check(!LowBridge(100), "a slot past the asphalt range does not match");
	Check(!LowBridge(204), "a slot before the concrete range does not match");
	Check(!LowBridge(231), "a slot past the concrete range does not match");
	Check(!LowBridge(0), "the empty overlay slot does not match");
	Check(!LowBridge(-1), "a null overlay index does not match");

	if (g_failures == 0)
	{
		std::printf("PASS (0 failures)\n");
		return 0;
	}

	std::printf("FAIL %d check(s) failed\n", g_failures);
	return 1;
}
