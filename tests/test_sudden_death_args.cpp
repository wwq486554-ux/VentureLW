// Unit test: the ReceiveDamage argument access used by the 0x701900 hook.
//
// TEST_DEPS:
// (header-only test: src/Features/SuddenDeath/ReceiveDamageArgs.h is header-only
// and engine-free apart from the Syringe register frame and the plain
// args_ReceiveDamage struct; the hook itself and its engine effect stay covered
// by verify_build.py, load_probe.sh and the in-game check.)
//
// Why this exists
// ---------------
// The 2026-11-20 real-machine test showed the feature completely inert: the hook
// had read [esp+4] as if it held an `args_ReceiveDamage*`, but the seven
// parameters are passed by value there, so the handler turned the damage
// pointer into a fake argument block.  This test builds a fake hook frame and
// pins the difference, so the mistake cannot come back unnoticed.

#include <cstddef>
#include <cstdio>

#include "Features/SuddenDeath/ReceiveDamageArgs.h"

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

// A hook frame at the function entry: [esp] is the return address, [esp+4] is
// the first parameter of the by-value argument block.
struct FakeFrame
{
	unsigned ReturnAddress;
	args_ReceiveDamage Args;
};

static_assert(offsetof(FakeFrame, Args) == 4,
	"the fake frame must put the argument block at [esp+4]");

unsigned g_returnAddress = 0x00401000;
int g_damage = 42;
} // namespace

int __cdecl main()
{
	using venture::features::sudden_death::GetReceiveDamageArgs;

	FakeFrame frame{};
	frame.ReturnAddress = g_returnAddress;
	frame.Args.Damage = &g_damage;
	frame.Args.DistanceToEpicenter = 7;
	frame.Args.WH = reinterpret_cast<WarheadTypeClass*>(0x1234);
	frame.Args.Attacker = reinterpret_cast<TechnoClass*>(0x5678);
	frame.Args.IgnoreDefenses = true;
	frame.Args.PreventsPassengerEscape = false;
	frame.Args.SourceHouse = reinterpret_cast<HouseClass*>(0x9ABC);

	REGISTERS registers{};
	registers.ESP(reinterpret_cast<DWORD>(&frame));

	args_ReceiveDamage* const args = GetReceiveDamageArgs(&registers);

	Check(args == &frame.Args, "the helper returns the address of the argument block");
	Check(args && args->WH == reinterpret_cast<WarheadTypeClass*>(0x1234),
		"the warhead field reads through the block");
	Check(args && args->Attacker == reinterpret_cast<TechnoClass*>(0x5678),
		"the attacker field reads through the block");
	Check(args && args->Damage == &g_damage && *args->Damage == 42,
		"the damage pointer field reads through the block");
	Check(args && args->DistanceToEpicenter == 7, "the distance field reads through the block");
	Check(args && args->IgnoreDefenses && !args->PreventsPassengerEscape,
		"the boolean fields read through the block");
	Check(args && args->SourceHouse == reinterpret_cast<HouseClass*>(0x9ABC),
		"the source house field reads through the block");

	// The failure mode this test pins: dereferencing [esp+4] yields the first
	// parameter (the damage pointer), not the block address.
	args_ReceiveDamage* const wrong = registers.Stack<args_ReceiveDamage*>(0x4);
	Check(wrong == reinterpret_cast<args_ReceiveDamage*>(&g_damage),
		"dereferencing [esp+4] reads the damage pointer (the old bug)");
	Check(wrong != args, "the dereference is not the argument block - LEA must be used");

	Check(GetReceiveDamageArgs(nullptr) == nullptr, "a null register frame is rejected");

	if (g_failures == 0)
	{
		std::printf("PASS (0 failures)\n");
		return 0;
	}

	std::printf("FAIL %d check(s) failed\n", g_failures);
	return 1;
}
