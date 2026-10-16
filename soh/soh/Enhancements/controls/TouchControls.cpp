#ifdef __ANDROID__

#include "TouchControls.h"

#include <SDL2/SDL.h>
#include <imgui.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/libultra/controller.h>
#include <ship/Context.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "soh/SohGui/SohMenu.h"
#include "soh/cvar_prefixes.h"
#include "z64.h"

namespace {

constexpr float kStickMax = 80.0f;

struct Finger {
    ImVec2 pos;
    SDL_FingerID id;
};

struct Button {
    uint16_t mask;
    const char* label;
    ImU32 color;
    ImVec2 center;
    float radius;
    bool pressed;
};

struct State {
    bool active = false;
    std::vector<Button> buttons;
    ImVec2 menuCenter;
    float menuRadius = 0.0f;
    bool menuDown = false;
    bool stickHeld = false;
    ImVec2 stickAnchor;
    ImVec2 stickPos;
    float stickTravel = 0.0f;
    SDL_FingerID stickFinger = -1;
};

State sState;

float Dist(const ImVec2& a, const ImVec2& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

bool Enabled() {
    return CVarGetInteger(CVAR_SETTING("TouchControls.Enabled"), 1) != 0;
}

void ComputeLayout(State& s, const ImVec2& display) {
    const float scale = std::clamp(CVarGetFloat(CVAR_SETTING("TouchControls.Scale"), 1.0f), 0.5f, 2.0f);
    const float u = std::min(display.x, display.y) * 0.055f * scale;
    const float m = u * 1.6f;
    const float w = display.x;
    const float h = display.y;

    const ImU32 green = IM_COL32(60, 200, 90, 255);
    const ImU32 red = IM_COL32(220, 70, 60, 255);
    const ImU32 yellow = IM_COL32(240, 200, 60, 255);
    const ImU32 grey = IM_COL32(170, 170, 180, 255);

    s.buttons.clear();
    const auto add = [&](uint16_t mask, const char* label, ImU32 color, float x, float y, float r) {
        s.buttons.push_back({ mask, label, color, ImVec2(x, y), r, false });
    };

    add(BTN_A, "A", green, w - m * 1.3f, h - m * 1.4f, u * 1.5f);
    add(BTN_B, "B", red, w - m * 2.6f, h - m * 0.9f, u * 1.2f);

    const float cx = w - m * 1.4f;
    const float cy = h * 0.40f;
    const float cs = u * 1.5f;
    add(BTN_CUP, "C^", yellow, cx, cy - cs, u * 0.95f);
    add(BTN_CDOWN, "Cv", yellow, cx, cy + cs, u * 0.95f);
    add(BTN_CLEFT, "C<", yellow, cx - cs, cy, u * 0.95f);
    add(BTN_CRIGHT, "C>", yellow, cx + cs, cy, u * 0.95f);

    add(BTN_Z, "Z", grey, m * 1.1f, h * 0.30f, u * 1.1f);
    add(BTN_L, "L", grey, m * 1.1f, h * 0.12f, u * 0.9f);
    add(BTN_R, "R", grey, w - m * 1.1f, h * 0.12f, u * 0.9f);
    add(BTN_START, "S", grey, w * 0.5f, h - m * 0.7f, u * 0.9f);

    s.menuCenter = ImVec2(w * 0.5f, m * 0.6f);
    s.menuRadius = u * 0.9f;
    s.stickTravel = u * 2.6f;
}

std::vector<Finger> GatherFingers(const ImVec2& display) {
    std::vector<Finger> fingers;
    for (int d = 0; d < SDL_GetNumTouchDevices(); d++) {
        const SDL_TouchID device = SDL_GetTouchDevice(d);
        if (device == 0) {
            continue;
        }
        for (int f = 0; f < SDL_GetNumTouchFingers(device); f++) {
            const SDL_Finger* finger = SDL_GetTouchFinger(device, f);
            if (finger != nullptr) {
                fingers.push_back({ ImVec2(finger->x * display.x, finger->y * display.y), finger->id });
            }
        }
    }
    return fingers;
}

// A pad or keyboard takes over, so the overlay only shows while no controller is attached.
bool ControllerAttached() {
    return SDL_NumJoysticks() > 0;
}

} // namespace

extern "C" void TouchControls_ApplyPad(void* pads) {
    sState.active = false;
    sState.stickHeld = false;

    if (ImGui::GetCurrentContext() == nullptr || !Enabled() || ControllerAttached()) {
        sState.stickFinger = -1;
        return;
    }

    const auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui == nullptr) {
        return;
    }

    const bool menuOpen = gui->GetMenuOrMenubarVisible();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    sState.active = true;
    ComputeLayout(sState, display);
    const std::vector<Finger> fingers = GatherFingers(display);

    bool menuDownNow = false;
    for (const auto& finger : fingers) {
        if (Dist(finger.pos, sState.menuCenter) <= sState.menuRadius * 1.3f) {
            menuDownNow = true;
            break;
        }
    }
    // Fires on release so the same tap can't also land on the menu that just opened.
    if (sState.menuDown && !menuDownNow && gui->GetMenu() != nullptr) {
        gui->GetMenu()->ToggleVisibility();
    }
    sState.menuDown = menuDownNow;

    if (menuOpen) {
        sState.stickFinger = -1;
        return;
    }

    OSContPad* pad = static_cast<OSContPad*>(pads);
    uint16_t buttons = 0;

    // Each finger presses the nearest button it overlaps; re-evaluated every tick so sliding works.
    SDL_FingerID stickFinger = -1;
    bool stickStillDown = false;
    for (const auto& finger : fingers) {
        if (finger.id == sState.stickFinger) {
            stickStillDown = true;
            stickFinger = finger.id;
            sState.stickPos = finger.pos;
            continue;
        }

        Button* best = nullptr;
        float bestDist = 0.0f;
        for (auto& button : sState.buttons) {
            const float d = Dist(finger.pos, button.center);
            if (d <= button.radius * 1.25f && (best == nullptr || d < bestDist)) {
                best = &button;
                bestDist = d;
            }
        }
        if (best != nullptr) {
            best->pressed = true;
            buttons |= best->mask;
        } else if (sState.stickFinger == -1 && stickFinger == -1 && finger.pos.x < display.x * 0.4f) {
            // The stick floats: it anchors wherever the thumb lands in the left side of the screen.
            stickFinger = finger.id;
            stickStillDown = true;
            sState.stickAnchor = finger.pos;
            sState.stickPos = finger.pos;
        }
    }
    sState.stickFinger = stickStillDown ? stickFinger : -1;

    if (sState.stickFinger != -1) {
        sState.stickHeld = true;
        float dx = sState.stickPos.x - sState.stickAnchor.x;
        float dy = sState.stickPos.y - sState.stickAnchor.y;
        const float len = std::hypot(dx, dy);
        if (len > sState.stickTravel) {
            // Drag the anchor along so reversing direction doesn't need a full swipe back.
            sState.stickAnchor.x += dx / len * (len - sState.stickTravel);
            sState.stickAnchor.y += dy / len * (len - sState.stickTravel);
            dx = sState.stickPos.x - sState.stickAnchor.x;
            dy = sState.stickPos.y - sState.stickAnchor.y;
        }
        pad[0].stick_x = static_cast<int8_t>(std::clamp(dx / sState.stickTravel, -1.0f, 1.0f) * kStickMax);
        pad[0].stick_y = static_cast<int8_t>(std::clamp(-dy / sState.stickTravel, -1.0f, 1.0f) * kStickMax);
    }

    pad[0].button |= buttons;
}

void TouchControlsOverlay::Draw() {
    if (!sState.active || !Enabled()) {
        return;
    }

    const float opacity = std::clamp(CVarGetFloat(CVAR_SETTING("TouchControls.Opacity"), 0.8f), 0.1f, 1.0f);
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();

    const auto alpha = [opacity](ImU32 color, float a) {
        const ImU32 rgb = color & ~IM_COL32_A_MASK;
        return rgb | (static_cast<ImU32>(std::min(a, 1.0f) * opacity * 255.0f) << IM_COL32_A_SHIFT);
    };
    const auto text = [&](const ImVec2& center, float size, const char* s, ImU32 color) {
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
        drawList->AddText(font, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), color, s);
    };

    for (const auto& b : sState.buttons) {
        drawList->AddCircleFilled(b.center, b.radius, alpha(b.color, b.pressed ? 0.55f : 0.18f));
        drawList->AddCircle(b.center, b.radius, alpha(b.color, 0.9f), 0, std::max(2.0f, b.radius * 0.08f));
        text(b.center, b.radius * 0.9f, b.label, alpha(IM_COL32_WHITE, 1.0f));
    }

    drawList->AddCircleFilled(sState.menuCenter, sState.menuRadius, alpha(IM_COL32(60, 60, 70, 255), 0.5f));
    text(sState.menuCenter, sState.menuRadius * 0.6f, "Menu", alpha(IM_COL32_WHITE, 1.0f));

    if (sState.stickHeld) {
        drawList->AddCircle(sState.stickAnchor, sState.stickTravel, alpha(IM_COL32_WHITE, 0.5f), 0, 3.0f);
        drawList->AddCircleFilled(sState.stickPos, sState.stickTravel * 0.4f, alpha(IM_COL32_WHITE, 0.55f));
    }
}

#endif // __ANDROID__
