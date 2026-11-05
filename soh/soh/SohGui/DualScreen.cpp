#include "DualScreen.h"

#include <algorithm>
#include <imgui_internal.h>

#include "soh/cvar_prefixes.h"
#include <libultraship/bridge/consolevariablebridge.h>

#ifdef __ANDROID__
#include <ship/window/gui/SecondaryScreen.h>
#define HAVE_SECONDARY_SCREEN 1
#endif

namespace DualScreen {

bool IsActive() {
#ifdef HAVE_SECONDARY_SCREEN
    return Ship::SecondaryScreen::IsActive();
#else
    return false;
#endif
}

ImVec2 GetGamePos() {
    return ImGui::GetMainViewport()->Pos;
}

ImVec2 GetGameSize() {
#ifdef HAVE_SECONDARY_SCREEN
    return Ship::SecondaryScreen::GetPrimarySize();
#else
    return ImGui::GetMainViewport()->Size;
#endif
}

ImVec2 GetGameCenter() {
    ImVec2 pos = GetGamePos();
    ImVec2 size = GetGameSize();
    return ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
}

static bool MenuOnSecondary() {
    return IsActive() && CVarGetInteger(CVAR_DUAL_SCREEN_MENU, 1) != 0;
}

ImVec2 GetMenuPos() {
#ifdef HAVE_SECONDARY_SCREEN
    if (MenuOnSecondary()) {
        return Ship::SecondaryScreen::GetSecondaryPos();
    }
#endif
    return ImGui::GetMainViewport()->WorkPos;
}

ImVec2 GetMenuSize() {
#ifdef HAVE_SECONDARY_SCREEN
    if (MenuOnSecondary()) {
        return Ship::SecondaryScreen::GetSecondarySize();
    }
    // The main viewport may span both displays; only use the game display's part of its work area.
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 game = GetGameSize();
    return ImVec2(std::min(vp->WorkSize.x, game.x - (vp->WorkPos.x - vp->Pos.x)),
                  std::min(vp->WorkSize.y, game.y - (vp->WorkPos.y - vp->Pos.y)));
#else
    return ImGui::GetMainViewport()->WorkSize;
#endif
}

ImVec2 GetMenuCenter() {
    ImVec2 pos = GetMenuPos();
    ImVec2 size = GetMenuSize();
    return ImVec2(pos.x + size.x * 0.5f, pos.y + size.y * 0.5f);
}

// --- Tracker arrangement ---

namespace {
enum class ArrangeTarget { None, Secondary, Primary };

ArrangeTarget sPendingTarget = ArrangeTarget::None;
int sArrangeFrame = -1;
ImVec2 sCursor;
float sShelfHeight = 0.0f;

void TargetRect(ArrangeTarget target, ImVec2& pos, ImVec2& size) {
#ifdef HAVE_SECONDARY_SCREEN
    if (target == ArrangeTarget::Secondary && IsActive()) {
        pos = Ship::SecondaryScreen::GetSecondaryPos();
        size = Ship::SecondaryScreen::GetSecondarySize();
        return;
    }
#endif
    pos = GetGamePos();
    size = GetGameSize();
}
} // namespace

void RequestArrangeTrackers(bool toSecondary) {
    sPendingTarget = toSecondary ? ArrangeTarget::Secondary : ArrangeTarget::Primary;
    // Apply on the next frame, so every tracker window gets placed during one full frame.
    sArrangeFrame = ImGui::GetFrameCount() + 1;
    sCursor = ImVec2(-1.0f, -1.0f);
    sShelfHeight = 0.0f;
}

// --- Secondary-screen tracker dock ---

namespace {
const char* const kDockHostName = "##SecondaryScreenTrackerDock";

ImGuiID SecondaryDockId() {
    return ImHashStr("SecondaryScreenTrackerDockSpace");
}

// A fixed, full-screen, invisible window on the secondary display hosting a dockspace. List trackers dock into it as
// tabs; the dockspace can't be moved, resized, split or undocked from, so finger drags only scroll.
void DrawSecondaryDockHost() {
    static int sLastFrame = -1;
    const int frame = ImGui::GetFrameCount();
    if (sLastFrame == frame) {
        return;
    }
    sLastFrame = frame;

    ImVec2 pos, size;
    TargetRect(ArrangeTarget::Secondary, pos, size);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin(kDockHostName, nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(SecondaryDockId(), ImVec2(0.0f, 0.0f),
                     ImGuiDockNodeFlags_NoUndocking | ImGuiDockNodeFlags_NoDockingSplit | ImGuiDockNodeFlags_NoResize |
                         ImGuiDockNodeFlags_NoWindowMenuButton | ImGuiDockNodeFlags_NoCloseButton |
                         ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}
} // namespace

bool IsDockedTracker(const std::string& name) {
#ifdef HAVE_SECONDARY_SCREEN
    if (!IsActive() || CVarGetInteger(CVAR_DUAL_SCREEN_TRACKER_DOCK, 1) == 0) {
        return false;
    }
    return name == "Check Tracker" || name == "Entrance Tracker" || name == "Hint Tracker";
#else
    return false;
#endif
}

void PlaceTrackerWindow(const std::string& name) {
    ImGuiWindow* window = ImGui::FindWindowByName(name.c_str());
    const int frame = ImGui::GetFrameCount();

    if (IsDockedTracker(name)) {
        DrawSecondaryDockHost();
        ImGui::SetNextWindowDockID(SecondaryDockId(), ImGuiCond_Always);
        return;
    }
    if (window != nullptr && window->DockId == SecondaryDockId()) {
        // Docking turned off or the secondary display went away: float it again on the game display.
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(GetGamePos().x + 40.0f, GetGamePos().y + 40.0f), ImGuiCond_Always);
        return;
    }

    if (sPendingTarget != ArrangeTarget::None && frame > sArrangeFrame) {
        sPendingTarget = ArrangeTarget::None; // the arrangement frame is over
    }

    if (sPendingTarget != ArrangeTarget::None && frame == sArrangeFrame) {
        if (window != nullptr && window->DockIsActive) {
            return; // docked windows stay where their dock is
        }
        ImVec2 areaPos, areaSize;
        TargetRect(sPendingTarget, areaPos, areaSize);
        const float margin = 8.0f;
        if (sCursor.x < 0.0f) {
            sCursor = ImVec2(areaPos.x + margin, areaPos.y + margin);
        }
        ImVec2 size = window != nullptr ? window->Size : ImVec2(300.0f, 300.0f);
        // Simple shelf packing: left to right, wrapping to a new row when the display's width is used up.
        if (sCursor.x + size.x > areaPos.x + areaSize.x - margin && sCursor.x > areaPos.x + margin) {
            sCursor.x = areaPos.x + margin;
            sCursor.y += sShelfHeight + margin;
            sShelfHeight = 0.0f;
        }
        ImGui::SetNextWindowPos(sCursor, ImGuiCond_Always);
        sCursor.x += size.x + margin;
        sShelfHeight = std::max(sShelfHeight, size.y);
        return;
    }

    // A window saved on the secondary display (imgui.ini) while that display is now missing would be stranded
    // off-screen: bring it back onto the game display.
#ifdef HAVE_SECONDARY_SCREEN
    // Wait a few seconds after startup first: the secondary display's surface takes a moment to come up.
    if (window != nullptr && !window->DockIsActive && !IsActive() && frame > 600) {
        ImVec2 gameSize = GetGameSize();
        if (window->Pos.x >= gameSize.x - 16.0f) {
            ImGui::SetNextWindowPos(ImVec2(std::max(0.0f, gameSize.x - window->Size.x - 8.0f), window->Pos.y),
                                    ImGuiCond_Always);
        }
    }
#endif
}

} // namespace DualScreen
