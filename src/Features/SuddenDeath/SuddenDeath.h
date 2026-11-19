#pragma once

#include <cstddef>

#include <GeneralStructures.h>

#include "Features/SuddenDeath/SuddenDeathMode.h"

// ---------------------------------------------------------------------------
// SuddenDeath - warhead-level instant kill, feature module (B6, R4).
//
// Ported from Phobos-LWModified (the fork's `SuddenDeath` warhead keys and its
// queue -> execute design), reimplemented on the platform's own engine binding
// instead of Phobos' extension classes.
//
// A warhead that writes `SuddenDeath=yes` kills whatever its detonation reaches:
// armour, Verses, health, veterancy, `Immune=yes`, Iron Curtain and Force Shield
// do not matter.  The hit is only *recorded* inside the damage call; the kill is
// executed at the head of the next logic frame, where destroying an object can
// no longer re-enter the object bookkeeping the detonation is walking (the fork
// lost a whole evening to an AnimClass being destroyed twice that way).
//
// The same detonation also sweeps the terrain of its `CellSpread` area: ore is
// cleared completely, bridges are collapsed through the engine's own IonCannon
// branch and terrain objects (trees, tiberium pillars) are removed without a
// health check.  Terrain never reaches the damage pipeline, so it cannot be part
// of the techno queue.
//
// State:
//   * the per-warhead configuration is a type-level local table filled while the
//     engine parses rules(md).ini (same pattern as ROFCurve).  Warhead types are
//     process-lifetime objects and are never saved;
//   * the queues and de-duplication tables are transient per-scenario state.  A
//     scenario clear (new match, retry, main menu, savegame load) drops them; see
//     ClearPending() and the platform's RegisterScenarioClearObserver API.
//   => no savegame change at all (unlike the fork, which stored the two keys in
//      its WarheadType extension and broke old savegames).
//
// The whole module is the `[Features] SuddenDeath` switch (default on); the
// behaviour itself is opt-in per warhead.  No platform INI keys: the two keys
// live in the warhead section of rules(md).ini, a dynamic section the INI
// takeover does not own.
// ---------------------------------------------------------------------------

class WarheadTypeClass;
class TechnoClass;
class ObjectClass;
class HouseClass;
class CCINIClass;

namespace venture::features::sudden_death
{
// The [Features] switch, resolved by the platform before Initialize().
bool IsEnabled();

// True once at least one warhead wrote `SuddenDeath=yes`.  The damage hook uses
// it as an allocation-free early out on every ReceiveDamage call.
bool HasConfiguredWarhead();

// One lookup for the hook paths: true and the resolved mode when the warhead is
// configured, false otherwise.
bool TryGetMode(const WarheadTypeClass* pWarhead, ExecutionMode& mode);

// Convenience for the cursor path (two weapon slots per cursor update).
bool IsSuddenDeathWarhead(const WarheadTypeClass* pWarhead);

// Reads `SuddenDeath` / `SuddenDeath.Mode` from the section the engine just
// parsed.  Called from the WarheadTypeClass::LoadFromINI epilogue.
void LoadWarheadConfig(WarheadTypeClass* pWarhead, CCINIClass* pINIClass);

// Records one hit.  Duplicates for the same target in one frame collapse into
// the first record.
void RegisterHit(
	TechnoClass* pTarget,
	ObjectClass* pAttacker,
	HouseClass* pAttackingHouse,
	ExecutionMode mode);

// Records one detonation whose terrain has to be swept.  A detonation fired by
// the sweep itself (the engine's IonCannon bridge branch) is ignored.
void RegisterDetonation(const CoordStruct& coords, double cellSpread);

// Executes everything recorded so far and always empties both queues.  Called
// once per logic frame, at the head of LogicClass::Update.  Must be cheap when
// there is nothing to do.
void ExecutePending();

// Drops every transient record.  Registered as a platform scenario-clear
// observer, so it runs on a new match, a retry, a return to the main menu and a
// savegame load.
void __cdecl ClearPending();

// True when the techno carries a SuddenDeath warhead in its primary or
// secondary weapon slot (veteran weapons resolved through the type).  Used by
// the three cursor hooks to keep the Attack cursor on an `Immune=yes` target.
bool TechnoHoldsSuddenDeathWeapon(const TechnoClass* pTechno);

// Diagnostics.
std::size_t ConfiguredWarheadCount();
std::size_t PendingTechnoCount();
std::size_t PendingTerrainCount();
} // namespace venture::features::sudden_death
