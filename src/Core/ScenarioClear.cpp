#include "Core/ScenarioClear.h"

#include <vector>

namespace
{
std::vector<venture::ScenarioClearObserver>& Observers()
{
	// Function-local static: the file-static registrars of the platform and the
	// feature modules may call RegisterScenarioClearObserver() before this
	// translation unit's own dynamic initialization would run.
	static std::vector<venture::ScenarioClearObserver> observers;
	return observers;
}
} // namespace

namespace venture
{
void __cdecl RegisterScenarioClearObserver(const ScenarioClearObserver observer)
{
	if (!observer)
		return;

	std::vector<ScenarioClearObserver>& observers = Observers();

	for (const ScenarioClearObserver existing : observers)
	{
		if (existing == observer)
			return;
	}

	observers.push_back(observer);
}

namespace scenario_clear
{
std::size_t __cdecl NotifyScenarioClearObservers()
{
	// A snapshot keeps the notifier safe against an observer that registers
	// another observer while it runs (see the contract in the header).  The
	// registry is tiny and this runs once per scenario, not per frame.
	const std::vector<ScenarioClearObserver> snapshot = Observers();
	std::size_t notified = 0;

	for (const ScenarioClearObserver observer : snapshot)
	{
		if (!observer)
			continue;

		observer();
		++notified;
	}

	return notified;
}

std::size_t ScenarioClearObserverCount()
{
	return Observers().size();
}

void ResetScenarioClearObserversForTest()
{
	Observers().clear();
}
} // namespace scenario_clear
} // namespace venture
