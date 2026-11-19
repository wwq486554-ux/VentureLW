#pragma once

#include <cstddef>

// ---------------------------------------------------------------------------
// Scenario-clear observers.
//
// A feature sometimes owns transient state that must never survive a scenario
// change: a queue of objects recorded in the current match, a de-duplication
// table of cells already swept, a pointer to the object whose cursor is being
// computed.  The platform already resets every extension store and the global
// extension index from its own 0x685659 hook (Platform::OnScenarioClear); this
// is the matching hook for the feature-owned state.
//
// Contract:
//   * an observer is a plain C function pointer (__cdecl, like every other
//     platform callback) with no arguments and no return value;
//   * registration is once per function pointer - a duplicate is ignored, a
//     null pointer is ignored;
//   * every observer runs once per scenario clear, after the platform reset its
//     extension state, so an observer may assume the stores are already empty;
//   * observers must not register another observer from inside the callback: the
//     notifier takes a snapshot before it calls them, so a registration made
//     there runs from the next scenario clear on;
//   * registration happens during Startup() (from a feature's Initialize) on
//     the game's main thread; registration after that is allowed but has no
//     use.
//
// The scenario clear is the engine's ResetScenario, reached by a new match, a
// mission retry, a return to the main menu and a savegame load (the object
// state reader calls it inside the load).  The platform's handler deliberately
// keeps the extension load window open during a savegame load; observers get no
// such special case: clear the transient state unconditionally.
//
// The registry is engine-free on purpose, so tests/test_scenario_clear.cpp can
// exercise it without a game process.
// ---------------------------------------------------------------------------

namespace venture
{
// The observer signature (explicit __cdecl: the project builds with /Gz).
using ScenarioClearObserver = void (__cdecl*)();

// Public platform API, also declared in Core/Platform.h.  Defined here, not in
// Platform.cpp, because this translation unit stays engine-free.
void __cdecl RegisterScenarioClearObserver(ScenarioClearObserver observer);

namespace scenario_clear
{
// Runs every registered observer and returns how many ran.  Called by
// Platform::OnScenarioClear(); not for feature code.
std::size_t __cdecl NotifyScenarioClearObservers();

// Introspection for diagnostics and tests.
std::size_t ScenarioClearObserverCount();
void ResetScenarioClearObserversForTest();
} // namespace scenario_clear
} // namespace venture
