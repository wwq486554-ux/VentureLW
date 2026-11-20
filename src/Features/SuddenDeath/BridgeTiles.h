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
//
// Low bridges are a second, different kind of bridge cell: they are overlays
// laid on the water, carry none of the bridge cell flags and never show up in
// the tile comparison above.  The engine destroys them from one more branch of
// the same MapClass::DamageArea section, and that branch tests the cell's
// OverlayTypeIndex against two hardcoded ranges:
//
//   0x48A217  mov eax,[edi+0x44]        ; CellClass::OverlayTypeIndex
//   0x48A21C  cmp eax,0x4A / 0x4A..0x63 ; 74..99   (asphalt low bridge run)
//   0x48A26D  mov eax,[edi+0x44]
//   0x48A274  cmp eax,0xCD / 0xCD..0xE6 ; 205..230 (concrete low bridge run)
//
// A hit inside either range then has to be the [General] IonCannonWarhead or win
// a RandomRanged(1, BridgeStrength) roll, and is handed to the engine's own
// run destroyer (0x57BAA0 / 0x57CCF0) together with the cell's map coords; that
// walks the whole bridge run and removes it cell by cell.  The two branches
// share the DestroyableBridges and warhead `Wall=yes` gates described above.
//
// The ranges are raw [OverlayTypes] indices, not overlay names: they are exactly
// what the engine compares, so this header mirrors them instead of looking for a
// "LOBRDG" name.  A rules file that orders [OverlayTypes] differently moves which
// of its overlays count as low-bridge triggers for ordinary weapons and for this
// feature alike (the shipped YR list puts LOBRDG01 at 77, inside the range; the
// family tail past slot 99 stays outside it for the engine too).
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

// The engine's low-bridge overlay index test (0x48A214..0x48A27F): the two
// hardcoded 26-slot ranges of MapClass::DamageArea.  Everything outside them is
// not a low-bridge trigger for the engine either, so the sweep does not ask the
// engine to destroy it.
inline bool IsEngineLowBridgeOverlayIndex(const int overlayIndex)
{
	return (overlayIndex >= 74 && overlayIndex <= 99)
		|| (overlayIndex >= 205 && overlayIndex <= 230);
}
} // namespace venture::features::sudden_death
