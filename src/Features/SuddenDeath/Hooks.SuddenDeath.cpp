// ---------------------------------------------------------------------------
// SuddenDeath feature - the eight engine hooks (B6, R4).
//
// Addresses, sizes and every "must return 0" convention are audited in
// docs/HookCatalogue.md (eight `Venture_SuddenDeath_*` rows).  The audit this
// round is recorded in the round's internal review: all windows were re-decoded
// against gamemd.exe, and each site either chains with every audited ecosystem
// handler (all return 0) or is free.
//
// The handlers below only translate engine state into the feature's own state;
// the kills and the terrain sweep run from ExecutePending() at the head of the
// logic frame, never inside the damage stack.  That split is the feature's whole
// reason for existing (see SuddenDeath.h).
// ---------------------------------------------------------------------------

#include <SpecificStructures.h>
#include <Syringe.h>

#include <Helpers/Macro.h>

#include "Engine/EngineCompat.h"
#include "Features/SuddenDeath/SuddenDeath.h"

using venture::features::sudden_death::ExecutionMode;
using venture::features::sudden_death::IsEnabled;
using venture::features::sudden_death::IsSuddenDeathWarhead;
using venture::features::sudden_death::TryGetMode;

// TechnoClass::ReceiveDamage entry (the front door of the shared damage
// settlement; BuildingClass reaches the same function).  The hit is only
// recorded here: the handler must not rewrite the damage, must not return a
// jump address and must return 0 on every path - this site is shared with the
// official Phobos / Kratos / Mix / fork handlers, and a non-zero return would
// skip them and the engine code after this window (the fork's "hollowed
// handler" incident).
DEFINE_HOOK(0x701900, Venture_SuddenDeath_HitRegister, 0x6)
{
	if (!IsEnabled() || !venture::features::sudden_death::HasConfiguredWarhead())
		return 0;

	GET(TechnoClass*, pTarget, ECX);
	GET_STACK(args_ReceiveDamage*, pArgs, 0x4);

	if (!pTarget || !pArgs || !pArgs->WH)
		return 0;

	ExecutionMode mode = ExecutionMode::Normal;

	if (!TryGetMode(pArgs->WH, mode))
		return 0;

	venture::features::sudden_death::RegisterHit(pTarget, pArgs->Attacker, pArgs->SourceHouse, mode);
	return 0;
}

// WarheadTypeClass::LoadFromINI success epilogue: ESI is the warhead type the
// engine just read and [esp+0x150] the rules INIClass.  Same address as the
// official Phobos / Ares / Kratos / Mix handlers (all size 5, all return 0).
DEFINE_HOOK(0x75DEA0, Venture_SuddenDeath_WarheadType_INI, 0x5)
{
	if (!IsEnabled())
		return 0;

	GET(WarheadTypeClass*, pItem, ESI);
	GET_STACK(CCINIClass*, pINI, 0x150);

	if (pItem && pINI)
		venture::features::sudden_death::LoadWarheadConfig(pItem, pINI);

	return 0;
}

// LogicClass::Update, the logic frame head: every recorded hit and every
// recorded detonation is executed here, before any object is updated.  The
// handler is on the hot path, so ExecutePending() exits on two empty() calls.
DEFINE_HOOK(0x55B4E1, Venture_SuddenDeath_LogicFrame, 0x5)
{
	if (!IsEnabled())
		return 0;

	venture::features::sudden_death::ExecutePending();
	return 0;
}

// The three cursor functions downgrade an Attack action to "cannot attack" when
// the target's type is Immune=yes.  A SuddenDeath warhead kills regardless of
// immunity, so while one is in hand that downgrade is wrong.  Each window is the
// five-byte `mov <action>,2`; returning its continuation address skips exactly
// that store (the non-immune path jumps to the same address).  The acting unit
// is in a callee-saved register at each site, so the fork's extra shared-context
// hook (0x4DDED0) is not needed.
DEFINE_HOOK(0x417F63, Venture_SuddenDeath_AircraftImmuneCursor, 0x5)
{
	enum
	{
		ContinueAfterDowngrade = 0x417F68
	};

	if (!IsEnabled())
		return 0;

	GET(TechnoClass*, pActor, ESI);

	return venture::features::sudden_death::TechnoHoldsSuddenDeathWeapon(pActor)
		? ContinueAfterDowngrade
		: 0;
}

DEFINE_HOOK(0x51F179, Venture_SuddenDeath_InfantryImmuneCursor, 0x5)
{
	enum
	{
		ContinueAfterDowngrade = 0x51F17E
	};

	if (!IsEnabled())
		return 0;

	GET(TechnoClass*, pActor, EDI);

	return venture::features::sudden_death::TechnoHoldsSuddenDeathWeapon(pActor)
		? ContinueAfterDowngrade
		: 0;
}

DEFINE_HOOK(0x74049A, Venture_SuddenDeath_UnitImmuneCursor, 0x5)
{
	enum
	{
		ContinueAfterDowngrade = 0x74049F
	};

	if (!IsEnabled())
		return 0;

	GET(TechnoClass*, pActor, ESI);

	return venture::features::sudden_death::TechnoHoldsSuddenDeathWeapon(pActor)
		? ContinueAfterDowngrade
		: 0;
}

// TechnoClass::GetFireError's armour / Verses gate: the engine has just loaded
// the target's armour index and is about to compare the warhead's Verses entry
// against zero, refusing to fire when it is 0%.  A SuddenDeath weapon is
// supposed to fire at anything, so jump to the engine's own "passed" point
// (0x6FCB8D) instead of replaying the load.  EDI holds the warhead here
// (loaded from [ebx+0xAC] at 0x6FCB56), EBX the weapon and EBP the target.
// The skipped store has no side effects: 0x6FCB8D writes only AL and 0x6FCB97
// reloads EAX in full.  Ares hooks the immediately following 0x6FCB6A with its
// own Verses gate - the continuation deliberately skips it.
DEFINE_HOOK(0x6FCB64, Venture_SuddenDeath_CanFireVerses, 0x6)
{
	enum
	{
		EnginePassed = 0x6FCB8D
	};

	if (!IsEnabled())
		return 0;

	GET(WarheadTypeClass*, pWarhead, EDI);

	return IsSuddenDeathWarhead(pWarhead) ? EnginePassed : 0;
}

// MapClass::DamageArea entry: record the detonation's centre and CellSpread so
// the terrain inside the area is swept at the next safe point.  The site is
// before the engine's damage==0 / warhead null / Inert early exits, so even a
// Damage=0 warhead triggers the sweep.  Every damage-delivery path funnels
// through this function.  ECX = the CoordStruct pointer, [ebp+0xC] = the warhead
// (the function's own frame is already set up).
DEFINE_HOOK(0x489286, Venture_SuddenDeath_DamageArea, 0x6)
{
	if (!IsEnabled())
		return 0;

	GET(CoordStruct*, pCoords, ECX);
	GET_BASE(WarheadTypeClass*, pWarhead, 0xC);

	if (!pCoords || !pWarhead || !IsSuddenDeathWarhead(pWarhead))
		return 0;

	venture::features::sudden_death::RegisterDetonation(*pCoords, pWarhead->CellSpread);
	return 0;
}
