#pragma once

// ---------------------------------------------------------------------------
// Bridge-cell detection: the part of the engine's own test that pbsnew8 did not
// need, because its MO test map's bridge cells carried the bridge cell flags.
//
// pbsnew8 (Phobos-LWModified, tag v0.5.0.0-alpha2-pbsnew8, SuddenDeath.cpp around
// line 214) triggers the engine's bridge collapse for a cell that is
// `ContainsBridge() || ContainsBridgeEx()`, i.e. `CellFlags & 0x100 | 0x500`.
// The platform port uses exactly the same condition.  On the original-YR maps
// whose bridge cells do not carry those flags that condition never matches,
// even though the engine itself destroys such bridges from a normal shell.
//
// The engine's MapClass::DamageArea does not use the flags as the entry test:
// at 0x489EC9 it takes the cell's `IsoTileTypeIndex`, subtracts the bridge
// tileset base ([0xAA0E28]) and adds 1, then compares the result with four
// consecutive slots of two `[General]` bridge tile groups whose indices live in
// the engine globals 0xABAD30 (`BridgeMiddle1`) and 0xAA1028 (`BridgeMiddle2`):
//
//   0x489F27  mov ebx,[0xABAD30]
//   0x489F2D  cmp esi,ebx          / +1 / +2 / +3
//   0x489F46  mov edx,[0xAA1028]
//   0x489F53  cmp esi,edx          / +1 / +2 / +3
//
// (0xABC1D0 `BridgeBottomLeft1` and 0xAA1540 `BridgeBottomLeft2` are the other
// two globals filled in the same block; the damage branch does not compare
// against them.)  `CellFlags` are still read inside the branch, but only to
// decide whether the Z-band test runs; a cell without `BridgeHead` jumps
// straight to the warhead checks at 0x489FC2.
//
// This header mirrors that comparison exactly, in an engine-free form so the
// arithmetic can be unit-tested.  SuddenDeath.cpp feeds it the real tile index
// and the three engine globals; the rest of the pbsnew8 recipe (the Z + 416
// adjustment, the IonCannonWarhead DamageArea call, BlowUpBridge and the
// BlownBridges de-duplication) is untouched.
// ---------------------------------------------------------------------------

namespace venture::features::sudden_death
{
// `base`, `middle1` and `middle2` are the values of the three engine globals.
// Returns false when any of them is zero (the tile types are not loaded yet, or
// this is not a build where they exist) - the caller then keeps the pbsnew8
// flag-only behaviour.
inline bool IsEngineBridgeTileIndex(
	const int tileIndex,
	const int base,
	const int middle1,
	const int middle2)
{
	if (base == 0 || middle1 == 0 || middle2 == 0)
		return false;

	const int adjusted = tileIndex - base + 1;

	return (adjusted >= middle1 && adjusted <= middle1 + 3)
		|| (adjusted >= middle2 && adjusted <= middle2 + 3);
}
} // namespace venture::features::sudden_death
