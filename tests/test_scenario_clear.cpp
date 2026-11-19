// Unit test: the scenario-clear observer registry of the platform core.
//
// TEST_DEPS: src/Core/ScenarioClear.cpp
//
// The registry is engine-free on purpose, so the callback contract documented in
// Core/ScenarioClear.h can be checked without a game process: de-duplication,
// null rejection, notification order, the snapshot taken before the callbacks
// run, and the reset used between test cases.

#include <cstdio>
#include <vector>

#include "Core/ScenarioClear.h"

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

std::vector<int> g_order;
int g_firstCalls = 0;
int g_secondCalls = 0;
int g_lateCalls = 0;

void __cdecl FirstObserver()
{
	++g_firstCalls;
	g_order.push_back(1);
}

void __cdecl SecondObserver()
{
	++g_secondCalls;
	g_order.push_back(2);
}

void __cdecl LateObserver()
{
	++g_lateCalls;
	g_order.push_back(3);
}

void __cdecl RegisteringObserver()
{
	// Registering from inside a callback is allowed; the new observer must not
	// run in the current notification round (the notifier takes a snapshot).
	venture::RegisterScenarioClearObserver(&LateObserver);
	g_order.push_back(9);
}

void Reset()
{
	venture::scenario_clear::ResetScenarioClearObserversForTest();
	g_order.clear();
	g_firstCalls = 0;
	g_secondCalls = 0;
	g_lateCalls = 0;
}
} // namespace

int __cdecl main()
{
	using venture::RegisterScenarioClearObserver;
	using venture::scenario_clear::NotifyScenarioClearObservers;
	using venture::scenario_clear::ScenarioClearObserverCount;

	// -- empty registry ------------------------------------------------------
	Reset();
	Check(ScenarioClearObserverCount() == 0, "a fresh registry is empty");
	Check(NotifyScenarioClearObservers() == 0, "notifying an empty registry runs nothing");

	// -- null and de-duplication --------------------------------------------
	RegisterScenarioClearObserver(nullptr);
	Check(ScenarioClearObserverCount() == 0, "a null observer is ignored");

	RegisterScenarioClearObserver(&FirstObserver);
	Check(ScenarioClearObserverCount() == 1, "the first observer is registered");

	RegisterScenarioClearObserver(&FirstObserver);
	Check(ScenarioClearObserverCount() == 1, "registering the same observer twice is ignored");

	RegisterScenarioClearObserver(&SecondObserver);
	Check(ScenarioClearObserverCount() == 2, "a second observer is registered");

	// -- notification order --------------------------------------------------
	const std::size_t notified = NotifyScenarioClearObservers();
	Check(notified == 2, "both observers are notified");
	Check(g_firstCalls == 1 && g_secondCalls == 1, "each observer runs exactly once");
	Check(g_order.size() == 2 && g_order[0] == 1 && g_order[1] == 2,
		"observers run in registration order");

	// -- snapshot semantics --------------------------------------------------
	Reset();
	RegisterScenarioClearObserver(&RegisteringObserver);
	const std::size_t firstRound = NotifyScenarioClearObservers();
	Check(firstRound == 1, "only the registered observer runs in the first round");
	Check(g_lateCalls == 0, "an observer registered during the round does not run in it");
	Check(ScenarioClearObserverCount() == 2, "the observer registered during the round is kept");

	const std::size_t secondRound = NotifyScenarioClearObservers();
	Check(secondRound == 2, "the late observer runs from the next round on");
	Check(g_lateCalls == 1, "the late observer ran exactly once");
	Check(g_order.size() == 3 && g_order[0] == 9 && g_order[1] == 9 && g_order[2] == 3,
		"the late observer runs after the registering observer");

	// -- reset helper --------------------------------------------------------
	Reset();
	Check(ScenarioClearObserverCount() == 0 && NotifyScenarioClearObservers() == 0,
		"the test reset empties the registry");

	if (g_failures == 0)
	{
		std::printf("PASS (0 failures)\n");
		return 0;
	}

	std::printf("FAIL %d check(s) failed\n", g_failures);
	return 1;
}
