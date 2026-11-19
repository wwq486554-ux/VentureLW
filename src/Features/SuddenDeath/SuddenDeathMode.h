#pragma once

// ---------------------------------------------------------------------------
// SuddenDeath execution mode - the engine-free half of the feature.
//
// `SuddenDeath.Mode` is a word key: the fork accepts exactly `normal` and
// `erase` (ASCII case-insensitive) and silently keeps the default `normal` for
// anything else.  The platform keeps the same spelling and reports the rejected
// value with a warning instead of staying silent (see docs/New-or-Enhanced-Logics.md).
//
// This header is engine-free so tests/test_sudden_death.cpp can check the parser
// without a game process.
// ---------------------------------------------------------------------------

namespace venture::features::sudden_death
{
enum class ExecutionMode
{
	// The engine's ordinary death path: wreck, DeathWeapon, revenge weapon and
	// kill credit all happen as usual.  The kill itself is a plain ReceiveDamage
	// call with the neutral C4Warhead and IgnoreDefenses=true.
	Normal = 0,
	// Remove the object without entering the death path at all: no wreck, no
	// DeathWeapon, no revenge weapon, no death animation, no kill credit to the
	// attacker (the kill is registered against the target's own owner).
	Erase = 1,
};

// Parses one `SuddenDeath.Mode` value.  Returns true and writes `mode` for
// `normal` / `erase` (any ASCII case), returns false and leaves `mode` alone for
// anything else (including null and the empty string).
inline bool ParseExecutionMode(const char* const text, ExecutionMode& mode)
{
	if (!text)
		return false;

	auto equalsIgnoreCase = [](const char* const left, const char* const right)
	{
		const char* a = left;
		const char* b = right;

		while (*a && *b)
		{
			char la = *a;
			char lb = *b;

			if (la >= 'A' && la <= 'Z')
				la = static_cast<char>(la - 'A' + 'a');
			if (lb >= 'A' && lb <= 'Z')
				lb = static_cast<char>(lb - 'A' + 'a');

			if (la != lb)
				return false;

			++a;
			++b;
		}

		return *a == *b;
	};

	if (equalsIgnoreCase(text, "normal"))
	{
		mode = ExecutionMode::Normal;
		return true;
	}

	if (equalsIgnoreCase(text, "erase"))
	{
		mode = ExecutionMode::Erase;
		return true;
	}

	return false;
}
} // namespace venture::features::sudden_death
