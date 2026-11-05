#pragma once

// SoH-side helpers for dual-screen devices (see libultraship's ship/window/gui/SecondaryScreen.h).
//
// The ImGui canvas spans both displays: the game display on the left, the secondary display on the right. These
// helpers decide where SoH's UI goes. On builds/devices without a secondary display they fall back to the main
// viewport, so callers don't need any #ifdefs.

#include <string>
#include <imgui.h>

#define CVAR_DUAL_SCREEN_MENU CVAR_SETTING("SecondaryScreen.Menu") // Main menu on the second screen (default on)
// Check/entrance/hint trackers docked full-screen (as tabs) on the second screen (default on)
#define CVAR_DUAL_SCREEN_TRACKER_DOCK CVAR_SETTING("SecondaryScreen.TrackerDock")

namespace DualScreen {

// True when a secondary display is attached and in use.
bool IsActive();

// Rect of the game (primary) display.
ImVec2 GetGamePos();
ImVec2 GetGameSize();
ImVec2 GetGameCenter();

// Rect the main menu, modals and popups should use: the secondary display when active and enabled, else the game
// display.
ImVec2 GetMenuPos();
ImVec2 GetMenuSize();
ImVec2 GetMenuCenter();

// Queue a one-shot re-layout of every open tracker window onto the secondary display (or back onto the game display
// when `toSecondary` is false). Applied over the next frame by PlaceTrackerWindow().
void RequestArrangeTrackers(bool toSecondary);

// True if this tracker window should be docked full-screen on the secondary display (check/entrance/hint trackers,
// while a secondary display is active and the option is on). Such windows must not use floating-window flags.
bool IsDockedTracker(const std::string& name);

// Called by tracker windows right before ImGui::Begin(). Docks list trackers into the fixed secondary-screen dock,
// undocks them again when that's no longer wanted, applies any pending arrangement and rescues windows left on a
// display that is no longer attached.
void PlaceTrackerWindow(const std::string& name);

} // namespace DualScreen
