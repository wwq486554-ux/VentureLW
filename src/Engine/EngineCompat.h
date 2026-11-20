#pragma once

#include <windows.h>

// ---------------------------------------------------------------------------
// Compile-time half of the engine binding gate.
//
// The platform compiles against a pinned YRpp revision (src/Core/EnginePin.h);
// scripts/linux/check_yrpp.py is the build-time half. This header is the
// compile-time half: it includes the pinned engine wrapper and asserts the ABI
// surface the base and its features actually touch. Wherever a new engine type
// or field is used, add an assertion here as well - a YRpp update that silently
// moves a field must fail the build, not corrupt a savegame at run time.
//
// Values below were measured with scripts/linux/abi_probe.sh against the pinned
// YRpp revision. They belong to the pin: update them together with the pin and
// document the review in docs/EngineBindings.md.
// ---------------------------------------------------------------------------

#include <YRPP.h>
#include <EventClass.h>
#include <Helpers/Macro.h>
#include <SpecificStructures.h>

#include "Core/EnginePin.h"

#include <cstddef>

// gamemd.exe is a 32-bit process; a 64-bit platform DLL can never be loaded.
static_assert(sizeof(void*) == 4, "VentureLW must be built as a 32-bit (Win32/x86) DLL");

// Engine class sizes (YRpp c6c522f77b58508588d9a8652c53356e4aef7a96).
static_assert(sizeof(AbstractClass) == 0x24, "AbstractClass ABI changed - re-check the YRpp pin");
static_assert(sizeof(ObjectClass) == 0xAC, "ObjectClass ABI changed - re-check the YRpp pin");
static_assert(sizeof(FootClass) == 0x6C0, "FootClass ABI changed - re-check the YRpp pin");
static_assert(sizeof(TechnoClass) == 0x520, "TechnoClass ABI changed - re-check the YRpp pin");
static_assert(sizeof(UnitClass) == 0x8E8, "UnitClass ABI changed - re-check the YRpp pin");
static_assert(sizeof(BulletClass) == 0x160, "BulletClass ABI changed - re-check the YRpp pin");
static_assert(sizeof(BulletTypeClass) == 0x2F8, "BulletTypeClass ABI changed - re-check the YRpp pin");
static_assert(sizeof(UnitTypeClass) == 0xE78, "UnitTypeClass ABI changed - re-check the YRpp pin");
static_assert(sizeof(WeaponTypeClass) == 0x160, "WeaponTypeClass ABI changed - re-check the YRpp pin");

// ArmedEngineers binding: the feature takes over the InfantryClass
// MouseOverObject vtable slot (0x7EB0CC, read-only .rdata - the write goes
// through VirtualProtect), chains to the previous implementation and reads the
// engineer flag plus the owning house.  The engine reads the same fields at
// 0x51E6B4 ([this+0x6C0]->[+0xEC3]) and elsewhere.
static_assert(sizeof(InfantryClass) == 0x6F0, "InfantryClass ABI changed - re-check the YRpp pin");
static_assert(sizeof(InfantryTypeClass) == 0xED0, "InfantryTypeClass ABI changed - re-check the YRpp pin");
static_assert(offsetof(InfantryClass, Type) == 0x6C0, "InfantryClass::Type moved - re-check the YRpp pin");
static_assert(offsetof(InfantryTypeClass, Engineer) == 0xEC3, "InfantryTypeClass::Engineer moved - re-check the YRpp pin");
static_assert(offsetof(TechnoClass, Owner) == 0x21C, "TechnoClass::Owner moved - re-check the YRpp pin");

// Temporal / ROFCurve binding (the features and the shared per-techno
// extension): the platform creates the TemporalClass instance the engine
// dereferences without a null check at 0x4694BB, corrects the per-frame warp
// step on the live instance at the shared 0x71AB08 tail, and scales the
// engine-computed rearm delay by the ROF curve ratio (the curve divides by the
// weapon's own ROF).  The TemporalClass layout and the techno/weapon fields they
// reach are therefore part of the binding.  The expected offsets are the ones the
// engine itself uses; they were measured with scripts/linux/abi_probe.sh and
// cross-checked against the engine's instruction listing.
static_assert(sizeof(TemporalClass) == 0x50, "TemporalClass ABI changed - re-check the YRpp pin");
static_assert(offsetof(TemporalClass, Owner) == 0x24, "TemporalClass::Owner moved - re-check the YRpp pin");
static_assert(offsetof(TemporalClass, Target) == 0x28, "TemporalClass::Target moved - re-check the YRpp pin");
static_assert(offsetof(TemporalClass, NextTemporal) == 0x40, "TemporalClass::NextTemporal moved - re-check the YRpp pin");
static_assert(offsetof(TemporalClass, PrevTemporal) == 0x44, "TemporalClass::PrevTemporal moved - re-check the YRpp pin");
static_assert(offsetof(TemporalClass, WarpRemaining) == 0x48, "TemporalClass::WarpRemaining moved - re-check the YRpp pin");
static_assert(offsetof(TemporalClass, WarpPerStep) == 0x4C, "TemporalClass::WarpPerStep moved - re-check the YRpp pin");
static_assert(offsetof(TechnoClass, TemporalImUsing) == 0x274, "TechnoClass::TemporalImUsing moved - re-check the YRpp pin");
// The dword Ares uses for its own techno extension pointer (its WeaponIndex_Warp
// byte lives at +0x0A inside that extension).
static_assert(offsetof(TechnoClass, align_154) == 0x154, "TechnoClass::align_154 moved - re-check the YRpp pin");
static_assert(offsetof(WeaponTypeClass, Damage) == 0xA4, "WeaponTypeClass::Damage moved - re-check the YRpp pin");
static_assert(offsetof(WeaponTypeClass, Warhead) == 0xAC, "WeaponTypeClass::Warhead moved - re-check the YRpp pin");
static_assert(offsetof(WarheadTypeClass, Temporal) == 0x15A, "WarheadTypeClass::Temporal moved - re-check the YRpp pin");
static_assert(offsetof(WeaponTypeClass, ROF) == 0xB0, "WeaponTypeClass::ROF moved - re-check the YRpp pin");
static_assert(offsetof(BulletClass, WeaponType) == 0x130, "BulletClass::WeaponType moved - re-check the YRpp pin");

// ViewZoom binding: the feature builds its enlarged surface pair and the two
// engine scratch buffers itself (engine operator new + the engine's own 0x24
// byte DSurface constructor 0x4BA5A0), swaps the Composite/Tile globals for the
// duration of the render window, reads the visible-cell list length at 0x6DA938
// ([ebx+0xE0]) and positions the camera from TacticalPos.
static_assert(sizeof(RectangleStruct) == 0x10, "RectangleStruct ABI changed - re-check the YRpp pin");
static_assert(sizeof(Point2D) == 0x8, "Point2D ABI changed - re-check the YRpp pin");
static_assert(sizeof(DSurface) == 0x24, "DSurface ABI changed - re-check the YRpp pin");
static_assert(offsetof(TacticalClass, TacticalPos) == 0xB0, "TacticalClass::TacticalPos moved - re-check the YRpp pin");
static_assert(offsetof(TacticalClass, VisibleCellCount) == 0xE0,
	"TacticalClass::VisibleCellCount moved - re-check the YRpp pin");

// INI layer: the platform walks the engine's parsed INI objects (rules-INI
// takeover), so the container layouts are part of the binding.
static_assert(sizeof(INIClass) == 0x40, "INIClass ABI changed - re-check the YRpp pin");
static_assert(sizeof(CCINIClass) == 0x58, "CCINIClass ABI changed - re-check the YRpp pin");
static_assert(sizeof(INIClass::INIEntry) == 0x28, "INIEntry ABI changed - re-check the YRpp pin");
static_assert(sizeof(INIClass::INISection) == 0x44, "INISection ABI changed - re-check the YRpp pin");

// Network event record: the platform's event channel writes its payload into the
// engine's 104-byte data union, so the record layout is part of the binding.
// (The payload starts at offset 7: type + executed flag + house index + frame.)
static_assert(sizeof(EventClass) == 111, "EventClass ABI changed - re-check the YRpp pin");

// SuddenDeath binding: the feature reads the warhead keys in the
// WarheadTypeClass::LoadFromINI epilogue, records hits from the
// TechnoClass::ReceiveDamage argument block, executes the ordinary kill through
// the C4 warhead, removes terrain through the engine's own death tail and
// collapses bridges through the IonCannon branch of MapClass::DamageArea.
static_assert(sizeof(args_ReceiveDamage) == 0x1C,
	"args_ReceiveDamage ABI changed - re-check the YRpp pin");
static_assert(offsetof(ObjectClass, Health) == 0x6C, "ObjectClass::Health moved - re-check the YRpp pin");
static_assert(offsetof(ObjectClass, IsOnMap) == 0x74, "ObjectClass::IsOnMap moved - re-check the YRpp pin");
static_assert(offsetof(ObjectClass, InLimbo) == 0x81, "ObjectClass::InLimbo moved - re-check the YRpp pin");
static_assert(offsetof(ObjectClass, IsAlive) == 0x90, "ObjectClass::IsAlive moved - re-check the YRpp pin");
static_assert(offsetof(ObjectClass, NextObject) == 0x30,
	"ObjectClass::NextObject moved - re-check the YRpp pin");
static_assert(offsetof(TechnoClass, BunkerLinkedItem) == 0x2E4,
	"TechnoClass::BunkerLinkedItem moved - re-check the YRpp pin");
static_assert(offsetof(WarheadTypeClass, CellSpread) == 0x124,
	"WarheadTypeClass::CellSpread moved - re-check the YRpp pin");
static_assert(offsetof(RulesClass, C4Warhead) == 0xFA8, "RulesClass::C4Warhead moved - re-check the YRpp pin");
static_assert(offsetof(RulesClass, IonCannonWarhead) == 0xFF0,
	"RulesClass::IonCannonWarhead moved - re-check the YRpp pin");
static_assert(offsetof(CellClass, MapCoords) == 0x24, "CellClass::MapCoords moved - re-check the YRpp pin");
static_assert(CellClass::BridgeHeight == 416,
	"CellClass::BridgeHeight changed - re-check the YRpp pin");
static_assert(sizeof(TerrainClass) == 0xE0, "TerrainClass ABI changed - re-check the YRpp pin");
static_assert(offsetof(TerrainClass, Type) == 0xC8, "TerrainClass::Type moved - re-check the YRpp pin");
static_assert(offsetof(TerrainClass, IsBurning) == 0xCC,
	"TerrainClass::IsBurning moved - re-check the YRpp pin");
static_assert(offsetof(TerrainClass, IsCrumbling) == 0xCD,
	"TerrainClass::IsCrumbling moved - re-check the YRpp pin");
// The building-level damage gate: a BridgeRepairHut whose type is also
// Immune=yes returns from BuildingClass::ReceiveDamage before
// TechnoClass::ReceiveDamage, so the SuddenDeath hit registration needs both
// fields (the feature jumps over the pair at 0x4423A3 for its own warheads).
static_assert(offsetof(TechnoTypeClass, Immune) == 0x233,
	"TechnoTypeClass::Immune moved - re-check the YRpp pin");
static_assert(offsetof(BuildingTypeClass, BridgeRepairHut) == 0x16B6,
	"BuildingTypeClass::BridgeRepairHut moved - re-check the YRpp pin");
static_assert(offsetof(BuildingClass, Type) == 0x520,
	"BuildingClass::Type moved - re-check the YRpp pin");

// Syringe register frame layout (Syringe.h, SYR_VER=2). A wrong sized frame
// silently corrupts every hook handler.
static_assert(sizeof(REGISTERS) == 0x28, "Syringe REGISTERS ABI changed - check the SyringeEx version");
