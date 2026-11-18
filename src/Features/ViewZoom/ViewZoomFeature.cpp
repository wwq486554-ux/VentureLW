// ---------------------------------------------------------------------------
// ViewZoom feature module (B1) - the platform adapter.
//
// Ported from Phobos-LWModified: the tactical map is rendered at a resolution of
// its own, so it can be zoomed in and out while the interface - sidebar, fonts,
// buttons, menus - keeps the resolution the game is set to.  The rendering and
// input implementation lives in ViewZoom.cpp (state machine) and
// Hooks.ViewZoom.cpp (the 22 hooks); this unit is the platform glue:
//
//   * the [Features] ViewZoom switch plus the 15 [VentureLW] keys.  The keys
//     keep their fork names (ViewZoom.*) but moved from the official [Phobos]
//     section of RA2MD.ini into the platform section [VentureLW] of
//     VentureLW.ini, where the platform INI registry validates them, logs the
//     effective values, feeds the unknown-key scan and the template dump.
//     Every key is declared unconditionally: the [Features] switch gates the
//     behaviour, not the registration (INI-05).
//   * the three player commands (zoom in / zoom out / reset).  They are
//     registered with the platform command registry, which appends them to the
//     engine's command list; the player binds a key in Options -> Keyboard.
//     `ViewZoom.WideView=false` keeps the zoom-out command out of the list
//     because it could not do anything.
//   * the startup log block: resolved switches and the level table.
//
// No savegame state: the zoom is per-match view state and every match starts at
// the native level (the two scenario hooks reset it).
//
// See docs/User-Interface.md.
// ---------------------------------------------------------------------------

#include "Core/Log.h"
#include "Command/Commands.h"
#include "Core/PlatformConfig.h"
#include "Engine/EngineCompat.h"
#include "Features/ViewZoom/ViewZoom.h"
#include "Features/ViewZoom/ViewZoomFeature.h"

#include <platform/Feature.h>

namespace venture::features::view_zoom
{
namespace
{
// The three engine command names.  They are the keys the game's keyboard
// configuration stores, and the platform convention is one flat namespace, so
// every name carries the platform prefix (docs/Commands.md).
constexpr const char* kZoomInCommandName = "Venture View Zoom In";
constexpr const char* kZoomOutCommandName = "Venture View Zoom Out";
constexpr const char* kResetCommandName = "Venture Reset View Zoom";

// Written by the platform before Initialize() runs.
bool g_featureEnabled = false;

// The fork's own master key, kept so a migrated configuration behaves the same.
bool g_legacyEnabled = true;

// Resolved switch: [Features] ViewZoom && ViewZoom.Enable.  Every hook gates on
// this, and ViewZoomClass::Enabled carries the same value.
bool g_enabled = false;

void ExecuteZoomIn()
{
	ViewZoomClass::Step(-1);
}

void ExecuteZoomOut()
{
	ViewZoomClass::Step(1);
}

void ExecuteReset()
{
	ViewZoomClass::ResetToNative("command");
}

// Registers the three command descriptors at load time, before the engine asks
// for its command list.  They start disabled and Initialize() enables them from
// the resolved switches; the engine registration hook reads the flags later
// (after Startup), so the INI values still apply.  A refused platform keeps its
// hooks inert and never reaches the engine's command list.
struct CommandRegistrar
{
	CommandRegistrar()
	{
		const Commands::RegisterResult results[] = {
			Commands::Register({
				kZoomInCommandName,
				L"Zoom In (Tactical View)",
				L"" VC_PLATFORM_NAME,
				L"Zoom the tactical map in one level",
				false,
				&ExecuteZoomIn,
			}),
			Commands::Register({
				kZoomOutCommandName,
				L"Zoom Out (Tactical View)",
				L"" VC_PLATFORM_NAME,
				L"Zoom the tactical map out one level (needs ViewZoom.WideView=true)",
				false,
				&ExecuteZoomOut,
			}),
			Commands::Register({
				kResetCommandName,
				L"Reset Tactical View Zoom",
				L"" VC_PLATFORM_NAME,
				L"Return the tactical view to the level that matches the game resolution",
				false,
				&ExecuteReset,
			}),
		};

		for (const Commands::RegisterResult result : results)
		{
			if (result != Commands::RegisterResult::Ok)
			{
				Log::Warn("[ViewZoom] a platform command could not be registered (result %d)",
					static_cast<int>(result));
			}
		}
	}
};

const CommandRegistrar g_commandRegistrar;

void __cdecl Initialize(const FeatureContext& context)
{
	// Read (and register with the INI registry) every key whether the module is
	// on or off - see the file header.
	ViewZoomClass::Settings settings{};

	const bool legacyEnabled = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.Enable", true,
		"master switch of the tactical view zoom; the [Features] ViewZoom switch must be on too");

	settings.WideView = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.WideView", false,
		"enable zooming out (k > 1); the engine then renders a larger viewport");
	settings.WholeMap = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.WholeMap", true,
		"append the runtime whole-map level to the level table");
	settings.MiddleClickReset = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.MiddleClickReset", true,
		"a middle click on the tactical view returns to the native level");
	settings.ShowHint = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.ShowHint", true,
		"log one line for every level change, middle-click reset and per-match reset");
	settings.DebugFill = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.DebugFill", false,
		"fill the map area with magenta instead of drawing the frame (A/B aid)");
	settings.Diagnostics = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.Diagnostics", false,
		"write a performance and geometry report every few seconds (debug level)");
	settings.NoBandFill = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.NoBandFill", false,
		"skip the first banded fill of the enlarged surface (A/B aid)");
	settings.SidebarNativeView = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.SidebarNativeView", true,
		"present the sidebar against the native viewport while zoomed out");
	settings.TranslateDirtyAreas = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.TranslateDirtyAreas", true,
		"move dirty rectangles produced outside the render window into the enlarged view");
	settings.WideCellEnqueue = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.WideCellEnqueue", false,
		"enqueue cells against the enlarged viewport (measured harmful; A/B aid)");
	settings.BandFillOnScroll = VENTURE_INI_BOOL(context, "VentureLW", "ViewZoom.BandFillOnScroll", false,
		"restart the banded fill on every camera move (measured expensive)");
	settings.FullRedrawEvery = VENTURE_INI_INT(context, "VentureLW", "ViewZoom.FullRedrawEvery", 8, -1, 600,
		"force one whole-viewport repaint every N frames; -1 disables, 0 measures and adapts");
	settings.MaxStaticSkip = VENTURE_INI_INT(context, "VentureLW", "ViewZoom.MaxStaticSkip", 4, 0, 600,
		"skip the downscale for at most N unchanged frames while zoomed out; 0 disables");

	char levels[512] = {};
	VENTURE_INI_STRING(context, "VentureLW", "ViewZoom.Levels",
		"0.25,0.33,0.5,0.67,1.0,1.25,1.5,2.0,2.5", levels, sizeof(levels),
		"level table, comma separated map resolution factors; k<1 magnifies, k>1 widens, "
		"full/whole/wholemap/all is the runtime whole-map level");
	settings.LevelsText = levels;

	g_legacyEnabled = legacyEnabled;
	g_enabled = g_featureEnabled && legacyEnabled;
	settings.Enable = g_enabled;

	ViewZoomClass::LoadFromSettings(settings);

	const bool zoomOutCommandEnabled = g_enabled && settings.WideView;
	const bool inSet = Commands::SetEnabled(kZoomInCommandName, g_enabled);
	const bool outSet = Commands::SetEnabled(kZoomOutCommandName, zoomOutCommandEnabled);
	const bool resetSet = Commands::SetEnabled(kResetCommandName, g_enabled);

	if (!inSet || !outSet || !resetSet)
	{
		Log::Warn("[ViewZoom] a platform command was not in the registry - its enabled state "
			"was not applied");
	}

	Log::Info("[ViewZoom] initialized (feature switch=%d, ViewZoom.Enable=%d, effective=%d, "
		"%d level(s))",
		g_featureEnabled ? 1 : 0, g_legacyEnabled ? 1 : 0, g_enabled ? 1 : 0,
		ViewZoomClass::LevelCount);

	Log::Info("[ViewZoom] commands: %s (bound in Options -> Keyboard), zoom out %s, reset %s",
		g_enabled ? "zoom in enabled" : "zoom in disabled",
		zoomOutCommandEnabled ? "enabled" : (g_enabled ? "disabled (ViewZoom.WideView=false)" : "disabled"),
		g_enabled ? "enabled" : "disabled");

	if (!g_enabled && g_featureEnabled)
	{
		Log::Info("[ViewZoom] ViewZoom.Enable=false - the module stays inert and the vanilla "
			"view is untouched");
	}
}

const FeatureDescriptor kDescriptor{
	"view_zoom", // id: internal identifier
	"ViewZoom",  // iniKey: switch in [Features]
	"ViewZoom: tactical map zoom independent of the UI resolution",
	0, // requiredHosts (AND): no host dependency
	0, // anyOfHosts (OR): no host dependency
	&g_featureEnabled,
	&Initialize,
};
} // namespace

bool IsEnabled()
{
	return g_enabled;
}

VENTURE_REGISTER_FEATURE(kDescriptor);
} // namespace venture::features::view_zoom
