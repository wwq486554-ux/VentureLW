// ---------------------------------------------------------------------------
// SuddenDeath feature - state, queues and the execution side (B6, R4).
//
// The header holds the full design summary; this unit is the engine-facing half:
//
//   * the type-level warhead table (`WarheadTypeClass* -> ExecutionMode`), filled
//     from the WarheadTypeClass::LoadFromINI epilogue;
//   * the two transient queues and the de-duplication tables (removed terrain,
//     blown bridges), dropped by the platform's scenario-clear observer;
//   * ExecutePending(), called at the head of every logic frame: the ordinary
//     kill (normal mode), the full erase sequence (erase mode) and the terrain /
//     ore / bridge sweep;
//   * the cursor helper the three `Immune=yes` downgrade hooks ask about.
//
// Everything below uses the pinned YRpp virtual calls only; there is no direct
// call to a raw engine address (the fork's implementation record had the two
// ObjectClass addresses it needed named the wrong way round - the audit for this
// round pinned them and the platform never needs them by address at all).
// ---------------------------------------------------------------------------

#include "Features/SuddenDeath/SuddenDeath.h"

#include "Features/SuddenDeath/BridgeTiles.h"

#include "Engine/EngineCompat.h"

#include <Helpers/Cast.h>
#include <Helpers/Iterators.h>

#include "Core/EngineIni.h"
#include "Core/Log.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace venture::features::sudden_death
{
namespace
{
// Per-warhead configuration.  Type-level and process-lifetime: warhead types are
// created once while rules(md).ini is parsed, so the pointer keys are stable and
// the table is deliberately never cleared by a scenario clear (the rules are not
// re-read either, exactly like the ROFCurve weapon table).
std::unordered_map<const WarheadTypeClass*, ExecutionMode> g_warheadModes;

struct TechnoRecord
{
	TechnoClass* Target;
	ObjectClass* Attacker;
	HouseClass* AttackingHouse;
	ExecutionMode Mode;
};

struct TerrainRecord
{
	CoordStruct Coords;
	double CellSpread;
};

// Transient, scenario-scoped state.  Raw pointers are safe because both queues
// are emptied at the head of every logic frame and dropped on every scenario
// clear; neither queue may carry a pointer across a frame.
std::vector<TechnoRecord> g_technoQueue;
std::vector<TerrainRecord> g_terrainQueue;

// Objects and cells already handled in this scenario.  The entries are kept
// until the scenario ends: a later object reusing a removed terrain's address
// costs that one object a SuddenDeath hit, which is the safe direction to err
// in.  The same table keeps two detonations in one frame from collapsing the
// same bridge twice.
std::vector<TerrainClass*> g_removedTerrain;
std::vector<CellClass*> g_blownBridges;

constexpr std::size_t kRemovedTerrainLimit = 256;
constexpr std::size_t kTerrainQueueLimit = 1024;

// ReduceTiberium's argument is a number of density steps, not damage: the cell
// keeps its density in a byte capped at 11 and the engine removes the overlay
// outright only when the argument reaches density + 1.  Anything above the cap
// therefore means "take all of it".
constexpr int kTiberiumClearance = 100;

// Log throttles: the first few records are evidence, a long battle must not
// flood the log.  Debug lines are already gated by the platform log level.
constexpr int kHitLogLimit = 8;
constexpr int kExecuteLogLimit = 8;
constexpr int kSkipLogLimit = 4;
constexpr int kTerrainLogLimit = 12;
constexpr int kCursorLogLimit = 8;
constexpr int kDetonationLogLimit = 16;
constexpr int kBridgeLogLimit = 24;

int g_hitLogs = 0;
int g_executeLogs = 0;
int g_skipLogs = 0;
int g_terrainLogs = 0;
int g_cursorLogs = 0;
int g_detonationLogs = 0;
int g_bridgeLogs = 0;
int g_bridgeProbeDumps = 0;
bool g_summaryLogged = false;
bool g_queueLimitWarned = false;
bool g_ionWarheadWarned = false;

// Set only around the normal-mode execution call of an immune bridge repair hut;
// see ForcedBuildingGateTarget().
TechnoClass* g_forcedBuildingGateTarget = nullptr;

// Set while the sweep itself asks the engine to collapse a bridge: the nested
// MapClass::DamageArea call reaches our own 0x489286 hook, and that detonation
// must not be recorded again (the IonCannon warhead is a data-driven choice and
// may even be the SuddenDeath warhead itself).
bool g_selfDetonation = false;

unsigned PtrOf(const void* const pointer)
{
	return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(pointer));
}

const char* IdOf(const TechnoClass* const pTechno)
{
	if (!pTechno)
		return "<null>";

	const TechnoTypeClass* const pType = pTechno->GetTechnoType();
	return pType && pType->ID && *pType->ID ? pType->ID : "?";
}

void LogQueueLimitOnce()
{
	if (g_queueLimitWarned)
		return;

	g_queueLimitWarned = true;
	Log::Warn("[SuddenDeath] more than %zu terrain records in one frame - further records are "
		"dropped (a pathological CellSpread / fire rate combination)",
		kTerrainQueueLimit);
}

// A raw pointer recorded in one frame may belong to an object that left the
// world (and was deleted) before the next frame.  Dereferencing it to read
// IsAlive would already be too late, so membership in the engine's own techno
// array is checked first - a deleted object is removed from that array, a
// pointer comparison is harmless.  Objects that are alive but in limbo are still
// in the array; the usual state guards below handle them.
// A bridge repair hut that is also Immune=yes returns from
// BuildingClass::ReceiveDamage before TechnoClass::ReceiveDamage unless the
// warhead is a SuddenDeath one (see the 0x4423A3 hook).  The normal-mode
// execution uses the neutral C4Warhead, so it must be marked separately.
bool IsImmuneBridgeRepairHut(const TechnoClass* const pTechno)
{
	const BuildingClass* const pBuilding = abstract_cast<BuildingClass*>(const_cast<TechnoClass*>(pTechno));

	if (!pBuilding || !pBuilding->Type)
		return false;

	return pBuilding->Type->BridgeRepairHut && pBuilding->Type->Immune;
}

bool IsInTechnoArray(const TechnoClass* const pTechno)
{
	for (int index = 0; index < TechnoClass::Array.Count; ++index)
	{
		if (TechnoClass::Array[index] == pTechno)
			return true;
	}

	return false;
}

void ExecuteTechnoQueue()
{
	if (g_technoQueue.empty())
		return;

	// Move the queue out first: a kill runs engine code that may record a new hit
	// (a DeathWeapon, a chain reaction).  Those records belong to the next frame.
	std::vector<TechnoRecord> queue;
	queue.swap(g_technoQueue);

	for (const TechnoRecord& record : queue)
	{
		TechnoClass* const pTarget = record.Target;

		// A frame has passed since the hit was recorded and the warhead's own
		// damage settled in it: the target may already be dead, gone or in limbo.
		if (!pTarget || !IsInTechnoArray(pTarget))
		{
			if (g_skipLogs < kSkipLogLimit)
			{
				++g_skipLogs;
				Log::Debug("[SuddenDeath] execute skip 0x%08X: left the world", PtrOf(pTarget));
			}
			continue;
		}

		if (!pTarget->IsAlive || pTarget->InLimbo || pTarget->Health <= 0)
		{
			if (g_skipLogs < kSkipLogLimit)
			{
				++g_skipLogs;
				Log::Debug("[SuddenDeath] execute skip 0x%08X %s: %s", PtrOf(pTarget), IdOf(pTarget),
					!pTarget->IsAlive ? "not alive" : (pTarget->InLimbo ? "in limbo" : "no health left"));
			}
			continue;
		}

		if (record.Mode == ExecutionMode::Erase)
		{
			if (g_executeLogs < kExecuteLogLimit)
			{
				++g_executeLogs;
				Log::Info("[SuddenDeath] erase 0x%08X %s (health %d)", PtrOf(pTarget), IdOf(pTarget),
					pTarget->Health);
			}

			// --- the erase sequence (see the header of this unit) --------------
			// 1) A bunker's contents are released first, or they disappear with
			//    the building and leave dangling pointers behind.
			BuildingClass* const pBuilding = abstract_cast<BuildingClass*>(pTarget);
			if (pBuilding && pTarget->BunkerLinkedItem)
				pBuilding->UnloadBunker();

			// 2) Passengers die the ordinary way - they are not SuddenDeath
			//    targets themselves.
			pTarget->KillPassengers(pTarget);

			// 3) Stop first, then unlink.  Keeping the object alive here is what
			//    keeps a co-loaded platform's building-death animation cleanup
			//    out of the sequence (the fork's LWDORK crash window).
			pTarget->Stun();
			pTarget->Limbo();

			// 4) The kill is registered against the target's own owner: it is a
			//    removal, not a kill credited to the attacker.
			pTarget->RegisterKill(pTarget->Owner);

			// 5) Announce, clear IsAlive, queue for deletion.  This re-enters
			//    Limbo through the virtual slot, which the InLimbo guard short
			//    circuits - the order is the one the reference implementation
			//    used and must not be reordered.
			pTarget->UnInit();
			continue;
		}

		WarheadTypeClass* const pC4Warhead =
			RulesClass::Instance ? RulesClass::Instance->C4Warhead : nullptr;

		if (!pC4Warhead)
		{
			if (g_skipLogs < kSkipLogLimit)
			{
				++g_skipLogs;
				Log::Warn("[SuddenDeath] normal kill skipped: RulesClass::C4Warhead is null");
			}
			continue;
		}

		// The ordinary exit, in the shape the engine already knows: the neutral
		// C4 warhead through the normal ReceiveDamage entry, defences ignored so
		// armour, immunity, Iron Curtain and shields cannot stop it, and the
		// attacker passed along so the kill is still credited to whoever fired.
		const int healthBefore = pTarget->Health;

		// A bridge repair hut whose type is also Immune=yes would take the
		// building-level early-out here again (the execution warhead is the
		// neutral C4, not the SuddenDeath one that delivered the hit).  Mark the
		// target for exactly this call; the 0x4423A3 hook lets it through.
		const bool forceBuildingGate = IsImmuneBridgeRepairHut(pTarget);

		if (forceBuildingGate)
			g_forcedBuildingGateTarget = pTarget;

		pTarget->ReceiveDamage(&pTarget->Health, 0, pC4Warhead, record.Attacker, true, false,
			record.AttackingHouse);

		if (forceBuildingGate)
			g_forcedBuildingGateTarget = nullptr;

		if (g_executeLogs < kExecuteLogLimit)
		{
			++g_executeLogs;
			Log::Info("[SuddenDeath] kill 0x%08X %s (health %d -> %d)", PtrOf(pTarget),
				IdOf(pTarget), healthBefore, pTarget->Health);
		}
	}
}

// Engine globals the bridge tile comparison needs (see BridgeTiles.h).  They are
// filled once when the [General] bridge tile groups are loaded (0x545C1E etc.);
// zero means "not loaded yet" and the helper falls back to the pbsnew8 flags.
constexpr std::uintptr_t kBridgeTileBaseGlobal = 0xAA0E28;    // tileset base
constexpr std::uintptr_t kBridgeMiddle1Global = 0xABAD30;     // BridgeMiddle1
constexpr std::uintptr_t kBridgeMiddle2Global = 0xAA1028;     // BridgeMiddle2

int ReadEngineGlobal(const std::uintptr_t address)
{
	return *reinterpret_cast<const int*>(address);
}

bool g_bridgeTileGlobalsWarned = false;

// pbsnew8's condition plus the engine's own tile-type test (BridgeTiles.h).
bool IsBridgeCell(const CellClass* const pCell)
{
	if (!pCell)
		return false;

	if (pCell->ContainsBridge() || pCell->ContainsBridgeEx())
		return true;

	const int tileIndex = pCell->IsoTileTypeIndex;
	const int base = ReadEngineGlobal(kBridgeTileBaseGlobal);
	const int middle1 = ReadEngineGlobal(kBridgeMiddle1Global);
	const int middle2 = ReadEngineGlobal(kBridgeMiddle2Global);

	if (base == 0 || middle1 == 0 || middle2 == 0)
	{
		if (!g_bridgeTileGlobalsWarned)
		{
			g_bridgeTileGlobalsWarned = true;
			Log::Warn("[SuddenDeath] the bridge tile-type globals are zero (base=0x%08X "
				"middle1=0x%08X middle2=0x%08X) - only flag-bearing bridge cells are detected",
				static_cast<unsigned>(base), static_cast<unsigned>(middle1),
				static_cast<unsigned>(middle2));
		}
		return false;
	}

	return IsEngineBridgeTileIndex(tileIndex, base, middle1, middle2);
}

// Low bridge pieces: overlays on the water, no bridge cell flags, no bridge
// tiles.  The engine tests the cell's overlay index against two hardcoded
// ranges (see BridgeTiles.h); mirroring that test is what makes the sweep ask
// the engine to destroy exactly the cells the engine itself would.
bool IsLowBridgeCell(const CellClass* const pCell)
{
	if (!pCell)
		return false;

	return IsEngineLowBridgeOverlayIndex(pCell->OverlayTypeIndex);
}

// Both bridge families, for the diagnostic counters only: the two are destroyed
// through different branches of MapClass::DamageArea but share the sweep.
bool IsBridgeLikeCell(const CellClass* const pCell)
{
	return IsBridgeCell(pCell) || IsLowBridgeCell(pCell);
}

bool IsBridgeAlreadyBlown(const CellClass* const pCell)
{
	return std::find(g_blownBridges.begin(), g_blownBridges.end(), pCell) != g_blownBridges.end();
}

void SweepTerrainObject(TerrainClass* const pTerrain)
{
	if (!pTerrain || pTerrain->InLimbo || pTerrain->IsBurning || pTerrain->IsCrumbling)
		return;

	// Health is deliberately not part of this test: a TerrainType that does not
	// write Strength= enters the world with Health 0, and the tiberium pillars of
	// this mod are exactly that kind of terrain - testing health would skip the
	// objects a SuddenDeath warhead is most wanted against.
	if (std::find(g_removedTerrain.begin(), g_removedTerrain.end(), pTerrain) != g_removedTerrain.end())
		return;

	const TerrainTypeClass* const pType = pTerrain->Type;

	if (!pType)
		return;

	if (g_terrainLogs < kTerrainLogLimit)
	{
		++g_terrainLogs;
		Log::Debug("[SuddenDeath] remove terrain %s 0x%08X", pType->ID && *pType->ID ? pType->ID : "?",
			PtrOf(pTerrain));
	}

	// The engine's own terrain death tail (TerrainClass::ReceiveDamage after its
	// crumbling branch): redraw the area, then leave the world through the same
	// virtual calls.  Terrain never reaches the damage pipeline, so this is the
	// only way out of the world for it.  The fork's DestroyAnim / DestroySound
	// hooks are Phobos extension keys that this mod does not use - skipping them
	// changes nothing with the shipped data.
	RectangleStruct rect{};
	pTerrain->GetRenderDimensions(&rect);

	if (TacticalClass::Instance)
		TacticalClass::Instance->RegisterDirtyArea(rect, false);

	pTerrain->Disappear(true);
	pTerrain->UnInit();

	if (g_removedTerrain.size() >= kRemovedTerrainLimit)
		g_removedTerrain.erase(g_removedTerrain.begin());

	g_removedTerrain.push_back(pTerrain);
}

void SweepCell(CellClass* const pCell)
{
	if (!pCell)
		return;

	// --- ore ---------------------------------------------------------------
	if (pCell->GetContainedTiberiumIndex() >= 0)
	{
		pCell->ReduceTiberium(kTiberiumClearance);

		if (g_terrainLogs < kTerrainLogLimit)
		{
			++g_terrainLogs;
			Log::Debug("[SuddenDeath] clear ore cell [%d,%d]", static_cast<int>(pCell->MapCoords.X),
				static_cast<int>(pCell->MapCoords.Y));
		}
	}

	// --- bridges -----------------------------------------------------------
	// The engine only collapses a bridge from the MapClass::DamageArea branch a
	// Wall=yes warhead reaches, and that branch rolls against BridgeStrength
	// first.  There is exactly one way past the roll: the warhead being the
	// engine's [CombatDamage] IonCannonWarhead.  Ask the engine to do it with
	// that warhead at damage 1, then run the high-bridge aftermath (units on the
	// deck, craters) through BlowUpBridge as well.
	//
	// Two kinds of cell reach that section: high-bridge cells (bridge flags, or
	// the [General] BridgeMiddle tile groups) and low-bridge cells (one of the
	// engine's two hardcoded overlay index ranges).  They share the gates but
	// not the rest: the high-bridge branch needs the Z of the deck point and the
	// engine only removes the other cells of the run, while the low-bridge branch
	// takes the cell's ground coords and removes the whole run itself.
	const bool isHighBridge = IsBridgeCell(pCell);
	const bool isLowBridge = !isHighBridge && IsLowBridgeCell(pCell);

	if ((isHighBridge || isLowBridge) && !IsBridgeAlreadyBlown(pCell))
	{
		// The bridge branch only considers a point inside
		// (Level - 2) * LevelHeight + base < Z <= (Level + 1) * LevelHeight + base.
		// GetCellCoords() returns the ground-relative height (0 on flat ground),
		// so the bridge height has to be added for the point to land on the deck.
		// Only the BridgeHead flag gates that Z test; a BridgeBody cell skips it,
		// and the low-bridge branch never looks at Z at all.
		CoordStruct coords = pCell->GetCellCoords();
		const bool isBridgeHead = isHighBridge && pCell->ContainsBridge();

		if (isBridgeHead)
			coords.Z += CellClass::BridgeHeight;

		if (g_bridgeLogs < kBridgeLogLimit)
		{
			++g_bridgeLogs;
			Log::Debug("[SuddenDeath] bridge candidate [%d,%d] flags=0x%08X tile=%d "
				"overlay=%d level=%d kind=%s head=%d z=%d",
				static_cast<int>(pCell->MapCoords.X), static_cast<int>(pCell->MapCoords.Y),
				static_cast<unsigned>(pCell->Flags), pCell->IsoTileTypeIndex,
				pCell->OverlayTypeIndex, static_cast<int>(pCell->Level),
				isHighBridge ? "high" : "low", isBridgeHead ? 1 : 0, coords.Z);
		}

		WarheadTypeClass* const pIonWarhead =
			RulesClass::Instance ? RulesClass::Instance->IonCannonWarhead : nullptr;

		DamageAreaResult result = DamageAreaResult::Missed;

		if (pIonWarhead)
		{
			g_selfDetonation = true;
			result = MapClass::DamageArea(coords, 1, nullptr, pIonWarhead, false, nullptr);
			g_selfDetonation = false;
		}
		else if (!g_ionWarheadWarned)
		{
			g_ionWarheadWarned = true;
			Log::Warn("[SuddenDeath] RulesClass::IonCannonWarhead is null - bridge cells "
				"cannot be collapsed");
		}

		// BlowUpBridge is the high-bridge aftermath only (units on the deck plus
		// the crater/effects); the engine's low-bridge branch removes the overlay
		// run itself and never calls it, so neither does the sweep.
		if (isHighBridge)
			pCell->BlowUpBridge();

		g_blownBridges.push_back(pCell);

		if (g_bridgeLogs < kBridgeLogLimit)
		{
			++g_bridgeLogs;
			Log::Debug("[SuddenDeath] collapse bridge cell [%d,%d] kind=%s result=%d",
				static_cast<int>(pCell->MapCoords.X), static_cast<int>(pCell->MapCoords.Y),
				isHighBridge ? "high" : "low", static_cast<int>(result));
		}
	}

	// --- terrain objects ---------------------------------------------------
	// Both content chains: GetTerrain(false) walks the normal object list and
	// GetTerrain(true) the alternate one that a bridge cell keeps aside.
	for (const bool alt : {false, true})
		SweepTerrainObject(pCell->GetTerrain(alt));
}

void ExecuteTerrainQueue()
{
	if (g_terrainQueue.empty())
		return;

	std::vector<TerrainRecord> queue;
	queue.swap(g_terrainQueue);

	for (const TerrainRecord& record : queue)
	{
		// The fork sweeps CellSpread + 0.5, which makes a plain CellSpread=1
		// warhead cover the ring around the impact cell as well.
		CellRangeIterator<CellClass>{}(CellClass::Coord2Cell(record.Coords), record.CellSpread + 0.5,
			[](CellClass* const pCell)
			{
				SweepCell(pCell);
				return true;
			});
	}
}

void LogConfigurationSummaryOnce()
{
	if (g_summaryLogged)
		return;

	g_summaryLogged = true;

	std::size_t normal = 0;
	std::size_t erase = 0;

	for (const auto& entry : g_warheadModes)
	{
		if (entry.second == ExecutionMode::Erase)
			++erase;
		else
			++normal;
	}

	Log::Info("[SuddenDeath] warhead configuration after rules parsing: %zu warhead(s) "
		"(normal=%zu, erase=%zu)",
		g_warheadModes.size(), normal, erase);
}
} // namespace

bool HasConfiguredWarhead()
{
	return !g_warheadModes.empty();
}

bool TryGetMode(const WarheadTypeClass* const pWarhead, ExecutionMode& mode)
{
	if (!pWarhead || g_warheadModes.empty())
		return false;

	const auto it = g_warheadModes.find(pWarhead);

	if (it == g_warheadModes.end())
		return false;

	mode = it->second;
	return true;
}

bool IsSuddenDeathWarhead(const WarheadTypeClass* const pWarhead)
{
	ExecutionMode ignored = ExecutionMode::Normal;
	return TryGetMode(pWarhead, ignored);
}

std::size_t ConfiguredWarheadCount()
{
	return g_warheadModes.size();
}

std::size_t PendingTechnoCount()
{
	return g_technoQueue.size();
}

std::size_t PendingTerrainCount()
{
	return g_terrainQueue.size();
}

void LoadWarheadConfig(WarheadTypeClass* const pWarhead, CCINIClass* const pINIClass)
{
	if (!pWarhead || !pINIClass)
		return;

	const char* const section = pWarhead->ID;

	if (!section || !*section)
		return;

	if (!EngineIni::ReadBool(pINIClass, section, "SuddenDeath", false))
	{
		// A re-parse may drop a configuration that used to be there.
		g_warheadModes.erase(pWarhead);
		return;
	}

	ExecutionMode mode = ExecutionMode::Normal;
	char modeText[32] = {};
	EngineIni::ReadString(pINIClass, section, "SuddenDeath.Mode", "normal", modeText, sizeof(modeText));

	if (!ParseExecutionMode(modeText, mode))
	{
		Log::Warn("[SuddenDeath] %s: SuddenDeath.Mode='%s' is not valid - expected normal or "
			"erase; the default normal is in effect",
			section, modeText);
		mode = ExecutionMode::Normal;
	}

	g_warheadModes[pWarhead] = mode;

	Log::Debug("[SuddenDeath] %s: SuddenDeath=yes, mode=%s", section,
		mode == ExecutionMode::Erase ? "erase" : "normal");
}

void RegisterHit(
	TechnoClass* const pTarget,
	ObjectClass* const pAttacker,
	HouseClass* const pAttackingHouse,
	const ExecutionMode mode)
{
	if (!pTarget)
		return;

	// Several projectiles in one frame may reach the same target; the first
	// record owns the kill (the mode is a property of the warhead, so they agree).
	const auto existing = std::find_if(g_technoQueue.begin(), g_technoQueue.end(),
		[pTarget](const TechnoRecord& record)
		{
			return record.Target == pTarget;
		});

	if (existing != g_technoQueue.end())
		return;

	g_technoQueue.push_back({pTarget, pAttacker, pAttackingHouse, mode});

	if (g_hitLogs < kHitLogLimit)
	{
		++g_hitLogs;
		Log::Debug("[SuddenDeath] queue 0x%08X %s (health %d, mode=%s)", PtrOf(pTarget), IdOf(pTarget),
			pTarget->Health, mode == ExecutionMode::Erase ? "erase" : "normal");
	}
}

void RegisterDetonation(const CoordStruct& coords, const double cellSpread)
{
	// The sweep asks the engine to collapse a bridge through DamageArea, and that
	// call reaches this very hook again.  Its detonation is ours, not a new one.
	if (g_selfDetonation)
		return;

	if (g_terrainQueue.size() >= kTerrainQueueLimit)
	{
		LogQueueLimitOnce();
		return;
	}

	g_terrainQueue.push_back({coords, cellSpread});

	if (g_detonationLogs < kDetonationLogLimit)
	{
		++g_detonationLogs;
		const CellStruct cell = CellClass::Coord2Cell(coords);

		// Count bridge cells twice: inside the radius the sweep will cover and in
		// a wider ring.  The pair tells a "the bridge was outside the area" case
		// apart from a "the map's bridge cells are not flagged" case - neither is
		// distinguishable from a plain "no bridge candidate" log line.  Low
		// bridges are counted as well, so "in sweep" also covers the overlay kind.
		int bridgesInSweep = 0;
		int bridgesNear = 0;

		CellRangeIterator<CellClass>{}(cell, cellSpread + 0.5, [&bridgesInSweep](CellClass* const pCell)
		{
			if (IsBridgeLikeCell(pCell))
				++bridgesInSweep;
			return true;
		});

		CellRangeIterator<CellClass>{}(cell, cellSpread + 2.5, [&bridgesNear](CellClass* const pCell)
		{
			if (IsBridgeLikeCell(pCell))
				++bridgesNear;
			return true;
		});

		const int destroyableBridges =
			ScenarioClass::Instance ? (ScenarioClass::Instance->SpecialFlags.DestroyableBridges ? 1 : 0)
									: -1;

		// One probe line with the raw cell data the engine's bridge branch uses:
		// if a map's bridge cells are not recognised, these numbers say whether
		// the impact cell is a bridge tile at all and how far the tile-index
		// arithmetic is from the two [General] BridgeMiddle groups.
		const int bridgeBase = ReadEngineGlobal(kBridgeTileBaseGlobal);
		const int bridgeMiddle1 = ReadEngineGlobal(kBridgeMiddle1Global);
		const int bridgeMiddle2 = ReadEngineGlobal(kBridgeMiddle2Global);
		const CellClass* const pCenter = MapClass::Instance.TryGetCellAt(cell);
		const int centerTile = pCenter ? pCenter->IsoTileTypeIndex : -1;
		const int centerOverlay = pCenter ? pCenter->OverlayTypeIndex : -1;
		const unsigned centerFlags = pCenter ? static_cast<unsigned>(pCenter->Flags) : 0u;
		const int centerLevel = pCenter ? static_cast<int>(pCenter->Level) : -1;
		const int centerAdjusted = centerTile - bridgeBase + 1;

		Log::Debug("[SuddenDeath] detonation cell [%d,%d] z=%d spread=%.2f bridge in sweep=%d "
			"near=%d destroyableBridges=%d",
			static_cast<int>(cell.X), static_cast<int>(cell.Y), coords.Z, cellSpread, bridgesInSweep,
			bridgesNear, destroyableBridges);
		Log::Debug("[SuddenDeath] bridge probe [%d,%d] tile=%d overlay=%d flags=0x%08X level=%d "
			"base=%d middle1=%d middle2=%d adjusted=%d",
			static_cast<int>(cell.X), static_cast<int>(cell.Y), centerTile, centerOverlay,
			centerFlags, centerLevel, bridgeBase, bridgeMiddle1, bridgeMiddle2, centerAdjusted);

		// For the first few detonations also dump the distinct cell shapes in the
		// 2-cell neighbourhood: if the projectile went off just beside the deck,
		// the bridge tile/overlay values are still visible here.
		if (g_bridgeProbeDumps < 4)
		{
			++g_bridgeProbeDumps;

			struct ProbeCell
			{
				int Tile;
				int Overlay;
				unsigned Flags;
				int Level;
			};

			ProbeCell seen[8] = {};
			int seenCount = 0;

			CellRangeIterator<CellClass>{}(cell, 2.5, [&seen, &seenCount](CellClass* const pCell)
			{
				if (!pCell || seenCount >= 8)
					return seenCount < 8;

				for (int index = 0; index < seenCount; ++index)
				{
					if (seen[index].Tile == pCell->IsoTileTypeIndex
						&& seen[index].Overlay == pCell->OverlayTypeIndex
						&& seen[index].Flags == static_cast<unsigned>(pCell->Flags)
						&& seen[index].Level == pCell->Level)
						return true;
				}

				seen[seenCount].Tile = pCell->IsoTileTypeIndex;
				seen[seenCount].Overlay = pCell->OverlayTypeIndex;
				seen[seenCount].Flags = static_cast<unsigned>(pCell->Flags);
				seen[seenCount].Level = pCell->Level;

				Log::Debug("[SuddenDeath] probe area [%d,%d] tile=%d overlay=%d flags=0x%08X "
					"level=%d",
					static_cast<int>(pCell->MapCoords.X), static_cast<int>(pCell->MapCoords.Y),
					pCell->IsoTileTypeIndex, pCell->OverlayTypeIndex,
					static_cast<unsigned>(pCell->Flags), static_cast<int>(pCell->Level));

				++seenCount;
				return seenCount < 8;
			});
		}
	}
}

void ExecutePending()
{
	if (!IsEnabled())
	{
		// Defensive: a feature switched off between two frames must not keep
		// pointers around.
		g_technoQueue.clear();
		g_terrainQueue.clear();
		return;
	}

	LogConfigurationSummaryOnce();
	ExecuteTechnoQueue();
	ExecuteTerrainQueue();
}

TechnoClass* ForcedBuildingGateTarget()
{
	return g_forcedBuildingGateTarget;
}

void __cdecl ClearPending()
{
	const std::size_t techno = g_technoQueue.size();
	const std::size_t terrain = g_terrainQueue.size();
	const std::size_t removedTerrain = g_removedTerrain.size();
	const std::size_t bridges = g_blownBridges.size();

	g_technoQueue.clear();
	g_terrainQueue.clear();
	g_removedTerrain.clear();
	g_blownBridges.clear();
	g_selfDetonation = false;
	g_summaryLogged = false;
	g_ionWarheadWarned = false;
	g_bridgeTileGlobalsWarned = false;
	g_forcedBuildingGateTarget = nullptr;
	g_hitLogs = 0;
	g_executeLogs = 0;
	g_skipLogs = 0;
	g_terrainLogs = 0;
	g_cursorLogs = 0;
	g_detonationLogs = 0;
	g_bridgeLogs = 0;
	g_bridgeProbeDumps = 0;

	if (techno || terrain || removedTerrain || bridges)
	{
		Log::Info("[SuddenDeath] scenario clear: dropped %zu pending techno record(s), %zu terrain "
			"record(s), %zu removed terrain object(s), %zu collapsed bridge cell(s)",
			techno, terrain, removedTerrain, bridges);
	}
}

bool TechnoHoldsSuddenDeathWeapon(const TechnoClass* const pTechno)
{
	if (!pTechno || !pTechno->IsAlive || g_warheadModes.empty())
		return false;

	TechnoTypeClass* const pType = pTechno->GetTechnoType();

	if (!pType)
		return false;

	// Read the type's weapon slots directly instead of the engine's GetWeapon:
	// this runs on every cursor update and slots 0/1 are always present.
	const bool elite = pTechno->Veterancy.IsElite();

	for (std::size_t index = 0; index < 2; ++index)
	{
		WeaponTypeClass* const pWeapon = pType->GetWeapon(index, elite).WeaponType;

		if (!pWeapon || !pWeapon->Warhead || !IsSuddenDeathWarhead(pWeapon->Warhead))
			continue;

		if (g_cursorLogs < kCursorLogLimit)
		{
			++g_cursorLogs;
			Log::Debug("[SuddenDeath] cursor bypass: %s holds %s", IdOf(pTechno),
				pWeapon->ID && *pWeapon->ID ? pWeapon->ID : "?");
		}

		return true;
	}

	return false;
}
} // namespace venture::features::sudden_death
