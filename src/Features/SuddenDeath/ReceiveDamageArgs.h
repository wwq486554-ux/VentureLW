#pragma once

#include <SpecificStructures.h>
#include <Syringe.h>

// ---------------------------------------------------------------------------
// Argument access at the TechnoClass::ReceiveDamage hook (0x701900).
//
// ReceiveDamage takes seven separate parameters and the engine passes them
// **by value on the stack**; there is no `args_ReceiveDamage*` argument.  At a
// hook installed on the function entry the block therefore starts at [esp+4]
// (the return address occupies [esp]) and has to be addressed, not dereferenced:
//
//   LEA_STACK(args_ReceiveDamage*, args, 0x4)   // correct: ESP + 4
//   GET_STACK(args_ReceiveDamage*, args, 0x4)   // wrong: reads the FIRST
//                                               // parameter (the damage
//                                               // pointer) and treats it as
//                                               // the block address
//
// The first R4 build made exactly that mistake: the configuration side worked
// (the rules parse logged one configured warhead), but every hit read garbage
// from `pDamage + 8` as its warhead, so nothing was ever recorded and the
// feature looked completely inert in game.  This helper keeps the access in one
// place and tests/test_sudden_death_args.cpp pins its semantics without a game
// process.
// ---------------------------------------------------------------------------

namespace venture::features::sudden_death
{
inline args_ReceiveDamage* GetReceiveDamageArgs(REGISTERS* const R)
{
	if (!R)
		return nullptr;

	return R->lea_Stack<args_ReceiveDamage*>(0x4);
}
} // namespace venture::features::sudden_death
