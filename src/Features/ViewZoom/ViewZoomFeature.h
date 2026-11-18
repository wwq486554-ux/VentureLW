#pragma once

// ---------------------------------------------------------------------------
// ViewZoom feature module - the platform switch.
//
// The module is on when both of these are true:
//   * [Features] ViewZoom=yes in <Platform>.ini (the platform module switch,
//     resolved by the feature registry before Initialize() runs), and
//   * [VentureLW] ViewZoom.Enable=true (the fork's own master key, kept for
//     configuration migration; default true).
//
// Every hook of the module gates on IsEnabled() and returns 0 when it is off,
// which lets SyringeEx replay the covered instructions - i.e. the vanilla
// behaviour.  ViewZoomClass reads the resolved value through its Enabled flag.
//
// See docs/User-Interface.md and docs/HookCatalogue.md.
// ---------------------------------------------------------------------------

namespace venture::features::view_zoom
{
bool IsEnabled();
} // namespace venture::features::view_zoom
