#ifdef __ANDROID__

// Touch layout and styling ported from the N64 pad in Lemuroid (lemuroid-touchinput),
// https://github.com/Swordfish90/Lemuroid, Copyright (C) Filippo Scognamiglio, GPL-3.0.
// Radial placement follows PadKit, https://github.com/Swordfish90/PadKit, Apache-2.0.
// Rewritten for ImGui; the license texts are in docs/licenses.

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
constexpr float kPi = 3.14159265f;
// The primary dial is 180dp at Lemuroid's default scale, so one dp is 1/180 of it.
constexpr float kPrimaryDialDp = 180.0f;
constexpr float kDeadZoneSq = 0.025f;

struct Finger {
    ImVec2 pos;
    SDL_FingerID id;
};

struct Anchor {
    ImVec2 pos;
    float size;
    uint16_t mask;
};

enum class Kind { Button, Face, Cross, Analog };
enum class Icon { None, Play, Menu, Dots };

struct Control {
    Kind kind = Kind::Button;
    ImVec2 center;
    float size = 0.0f;
    uint16_t mask = 0;
    const char* label = nullptr;
    Icon icon = Icon::None;
    bool isMenu = false;
    bool isToggle = false;
    bool triangles = false;
    std::vector<Anchor> anchors;
    std::vector<const char*> labels;
    size_t primaryCount = 0;
    std::vector<SDL_FingerID> tracked;
    uint16_t down = 0;
    bool pressed = false;
    ImVec2 start;
    ImVec2 stick;
};

struct State {
    bool active = false;
    std::vector<Control> controls;
    ImVec2 layoutDisplay;
    float layoutScale = 0.0f;
    float dp = 1.0f;
    bool menuDown = false;
    bool toggleDown = false;
};

State sState;

struct Satellite {
    float degrees;
    float scale;
};

struct Radial {
    ImVec2 center;
    float size = 0.0f;
    std::vector<ImVec2> satCenters;
    std::vector<float> satSizes;
};

float Dist(const ImVec2& a, const ImVec2& b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

bool Enabled() {
    return CVarGetInteger(CVAR_SETTING("TouchControls.Enabled"), 1) != 0;
}

float SizeAroundRing(int count) {
    const float s = std::sin(kPi / count);
    return s / (1.0f - s);
}

float SizeOnRing(int count) {
    const float s = std::sin(kPi / std::max(count, 2));
    return s / (1.0f + s);
}

std::vector<Anchor> RingAnchors(const std::vector<uint16_t>& masks, float rotationDegrees, bool withComposites) {
    const int n = static_cast<int>(masks.size());
    const float rotation = rotationDegrees * kPi / 180.0f;
    std::vector<Anchor> anchors;
    for (int i = 0; i < n; i++) {
        const float angle = rotation + 2.0f * kPi * i / n;
        anchors.push_back({ ImVec2(std::cos(angle), std::sin(angle)), SizeOnRing(n), masks[i] });
    }
    if (withComposites) {
        for (int i = 0; i < n; i++) {
            const float angle = rotation + 2.0f * kPi * (i + 0.5f) / n;
            anchors.push_back({ ImVec2(std::cos(angle) * 0.85f, std::sin(angle) * 0.85f), 0.1f,
                                static_cast<uint16_t>(masks[i] | masks[(i + 1) % n]) });
        }
    }
    return anchors;
}

const Anchor* NearestAnchor(const std::vector<Anchor>& anchors, const ImVec2& p) {
    const Anchor* best = nullptr;
    float bestDist = 0.0f;
    for (const auto& anchor : anchors) {
        const float d = std::max(Dist(p, anchor.pos) - anchor.size, 0.0f);
        if (best == nullptr || d < bestDist) {
            best = &anchor;
            bestDist = d;
        }
    }
    return best;
}

Radial PlaceRadial(const std::vector<Satellite>& satellites, bool rightSide, const ImVec2& display, float marginX,
                   float marginY, float maxSize) {
    const float base = SizeAroundRing(12);
    float minX = -0.5f;
    float minY = -0.5f;
    float maxX = 0.5f;
    float maxY = 0.5f;
    std::vector<ImVec2> offsets;
    for (const auto& sat : satellites) {
        const float angle = -sat.degrees * kPi / 180.0f;
        const float distance = 0.5f * (1.0f + base * sat.scale);
        const ImVec2 pos(std::cos(angle) * distance, std::sin(angle) * distance);
        const float radius = base * sat.scale * 0.5f;
        minX = std::min(minX, pos.x - radius);
        minY = std::min(minY, pos.y - radius);
        maxX = std::max(maxX, pos.x + radius);
        maxY = std::max(maxY, pos.y + radius);
        offsets.push_back(pos);
    }
    const float width = maxX - minX;
    const float height = maxY - minY;

    Radial radial;
    radial.size = std::min({ (display.x * 0.5f - marginX) / width, (display.y - marginY * 2.0f) / height, maxSize });
    const float left = rightSide ? display.x - marginX - radial.size * width : marginX;
    const float top = display.y - marginY - radial.size * height;
    radial.center = ImVec2(left - minX * radial.size, top - minY * radial.size);
    for (size_t i = 0; i < satellites.size(); i++) {
        radial.satCenters.push_back(
            ImVec2(radial.center.x + offsets[i].x * radial.size, radial.center.y + offsets[i].y * radial.size));
        radial.satSizes.push_back(radial.size * base * satellites[i].scale);
    }
    return radial;
}

Control MakeButton(const ImVec2& center, float size, uint16_t mask, const char* label, Icon icon = Icon::None,
                   bool isMenu = false, bool isToggle = false) {
    Control control;
    control.kind = Kind::Button;
    control.center = center;
    control.size = size;
    control.mask = mask;
    control.label = label;
    control.icon = icon;
    control.isMenu = isMenu;
    control.isToggle = isToggle;
    return control;
}

Control MakeRing(Kind kind, const ImVec2& center, float size, const std::vector<uint16_t>& masks, float rotationDegrees,
                 bool withComposites) {
    Control control;
    control.kind = kind;
    control.center = center;
    control.size = size;
    control.anchors = RingAnchors(masks, rotationDegrees, withComposites);
    control.primaryCount = masks.size();
    return control;
}

void ComputeLayout(State& s, const ImVec2& display, float scale) {
    const float marginX = display.y * 0.07f;
    const float marginY = display.y * 0.04f;
    const float maxSize = display.y * 0.34f * scale;

    const Radial left = PlaceRadial({ { -80.0f, 2.0f }, { 120.0f, 1.0f }, { 60.0f, 1.0f }, { -120.0f, 1.0f } }, false,
                                    display, marginX, marginY, maxSize);
    const Radial right = PlaceRadial({ { 60.0f, 1.0f }, { 120.0f, 1.0f }, { -100.0f, 2.0f }, { -60.0f, 1.0f } }, true,
                                     display, marginX, marginY, maxSize);

    s.dp = left.size / kPrimaryDialDp;
    const float inset = 8.0f * s.dp;

    s.controls.clear();

    s.controls.push_back(MakeRing(Kind::Cross, left.center, left.size - inset,
                                  { BTN_DRIGHT, BTN_DDOWN, BTN_DLEFT, BTN_DUP }, 0.0f, true));

    Control analog;
    analog.kind = Kind::Analog;
    analog.center = left.satCenters[0];
    analog.size = left.satSizes[0] - inset;
    s.controls.push_back(analog);

    s.controls.push_back(MakeButton(left.satCenters[1], left.satSizes[1] - inset, BTN_L, "L"));
    s.controls.push_back(MakeButton(left.satCenters[2], left.satSizes[2] - inset, BTN_Z, "Z"));

    Control faces = MakeRing(Kind::Face, right.center, right.size - inset, { BTN_A, BTN_B, BTN_Z }, 90.0f, true);
    faces.labels = { "A", "B", "Z" };
    s.controls.push_back(faces);

    s.controls.push_back(MakeButton(right.satCenters[0], right.satSizes[0] - inset, BTN_R, "R"));
    s.controls.push_back(MakeButton(right.satCenters[1], right.satSizes[1] - inset, BTN_START, nullptr, Icon::Play));

    Control cButtons = MakeRing(Kind::Cross, right.satCenters[2], right.satSizes[2] - inset,
                                { BTN_CRIGHT, BTN_CDOWN, BTN_CLEFT, BTN_CUP }, 0.0f, false);
    cButtons.triangles = true;
    s.controls.push_back(cButtons);

    s.controls.push_back(MakeButton(right.satCenters[3], right.satSizes[3] - inset, 0, nullptr, Icon::Menu, true));

    const float toggleSize = left.satSizes[1] * 0.8f;
    s.controls.push_back(MakeButton(ImVec2(display.x * 0.5f, marginY + toggleSize * 0.5f), toggleSize, 0, nullptr,
                                    Icon::Dots, false, true));

    s.layoutDisplay = display;
    s.layoutScale = scale;
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
    // SDL lists the built-in accelerometer as a joystick on Android, it isn't a game controller.
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            return true;
        }
    }
    return false;
}

ImVec2 ToDialSpace(const Control& c, const ImVec2& pos) {
    const float half = c.size * 0.5f;
    return ImVec2((pos.x - c.center.x) / half, (pos.y - c.center.y) / half);
}

void ApplyFinger(Control& c, const ImVec2& pos) {
    const ImVec2 p = ToDialSpace(c, pos);
    switch (c.kind) {
        case Kind::Face:
            c.down |=
                NearestAnchor(c.anchors, ImVec2(std::clamp(p.x, -1.0f, 1.0f), std::clamp(p.y, -1.0f, 1.0f)))->mask;
            break;
        case Kind::Cross:
            if (p.x * p.x + p.y * p.y >= kDeadZoneSq) {
                c.down |= NearestAnchor(c.anchors, p)->mask;
            }
            break;
        case Kind::Analog: {
            const float half = c.size * 0.5f;
            float dx = (pos.x - c.start.x) / half;
            float dy = (c.start.y - pos.y) / half;
            const float len = std::hypot(dx, dy);
            // Clamped to a circle rather than PadKit's square so diagonals stay inside the N64 gate.
            if (len > 1.0f) {
                dx /= len;
                dy /= len;
            }
            c.stick = ImVec2(dx, dy);
            break;
        }
        case Kind::Button:
            break;
    }
}

void ReleaseAll(State& s) {
    for (auto& c : s.controls) {
        c.tracked.clear();
        c.down = 0;
        c.pressed = false;
        c.stick = ImVec2();
    }
}

void UpdateControls(State& s, const std::vector<Finger>& fingers, bool hidden) {
    for (auto& c : s.controls) {
        c.pressed = false;
        c.down = 0;
        c.tracked.erase(std::remove_if(c.tracked.begin(), c.tracked.end(),
                                       [&fingers](SDL_FingerID id) {
                                           return std::none_of(fingers.begin(), fingers.end(),
                                                               [id](const Finger& f) { return f.id == id; });
                                       }),
                        c.tracked.end());
        if (c.tracked.empty()) {
            c.stick = ImVec2();
        }
    }

    for (const auto& finger : fingers) {
        Control* owner = nullptr;
        for (auto& c : s.controls) {
            if (std::find(c.tracked.begin(), c.tracked.end(), finger.id) != c.tracked.end()) {
                owner = &c;
                break;
            }
        }

        if (owner == nullptr) {
            for (auto& c : s.controls) {
                if ((hidden && !c.isToggle) || Dist(finger.pos, c.center) > c.size * 0.5f) {
                    continue;
                }
                if (c.kind == Kind::Button) {
                    c.pressed = true;
                } else if (c.kind == Kind::Face || c.tracked.empty()) {
                    c.tracked.push_back(finger.id);
                    c.start = finger.pos;
                    owner = &c;
                }
                break;
            }
        }

        if (owner != nullptr) {
            ApplyFinger(*owner, finger.pos);
        }
    }
}

ImVec2 Rotated(const ImVec2& p, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return ImVec2(p.x * c - p.y * s, p.x * s + p.y * c);
}

void DrawTriangle(ImDrawList* drawList, const ImVec2& center, float size, float radians, ImU32 color) {
    const float k = size / 24.0f;
    const ImVec2 pts[3] = { { -5.5f, -7.0f }, { 5.5f, 0.0f }, { -5.5f, 7.0f } };
    ImVec2 out[3];
    for (int i = 0; i < 3; i++) {
        const ImVec2 r = Rotated(pts[i], radians);
        out[i] = ImVec2(center.x + r.x * k, center.y + r.y * k);
    }
    drawList->AddTriangleFilled(out[0], out[1], out[2], color);
}

void DrawChevron(ImDrawList* drawList, const ImVec2& center, float size, float radians, ImU32 color) {
    const float k = size / 24.0f;
    const ImVec2 pts[3] = { { -3.0f, -6.0f }, { 3.0f, 0.0f }, { -3.0f, 6.0f } };
    ImVec2 out[3];
    for (int i = 0; i < 3; i++) {
        const ImVec2 r = Rotated(pts[i], radians);
        out[i] = ImVec2(center.x + r.x * k, center.y + r.y * k);
    }
    drawList->AddPolyline(out, 3, color, 0, 2.0f * k);
}

void DrawMenuIcon(ImDrawList* drawList, const ImVec2& center, float size, ImU32 color) {
    const float k = size / 24.0f;
    for (const float y : { -6.0f, -1.0f, 4.0f }) {
        drawList->AddRectFilled(ImVec2(center.x - 9.0f * k, center.y + y * k),
                                ImVec2(center.x + 9.0f * k, center.y + (y + 2.0f) * k), color);
    }
}

void DrawDotsIcon(ImDrawList* drawList, const ImVec2& center, float size, ImU32 color) {
    const float k = size / 24.0f;
    for (const float x : { -4.5f, 4.5f }) {
        for (const float y : { -4.5f, 4.5f }) {
            drawList->AddCircleFilled(ImVec2(center.x + x * k, center.y + y * k), 2.8f * k, color);
        }
    }
}

} // namespace

extern "C" void TouchControls_ApplyPad(void* pads) {
    sState.active = false;

    if (ImGui::GetCurrentContext() == nullptr || !Enabled() || ControllerAttached()) {
        ReleaseAll(sState);
        return;
    }

    const auto gui = Ship::Context::GetRawInstance()->GetWindow()->GetGui();
    if (gui == nullptr) {
        return;
    }

    const bool menuOpen = gui->GetMenuOrMenubarVisible();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float scale = std::clamp(CVarGetFloat(CVAR_SETTING("TouchControls.Scale"), 1.0f), 0.5f, 2.0f);
    sState.active = true;
    if (sState.controls.empty() || sState.layoutDisplay.x != display.x || sState.layoutDisplay.y != display.y ||
        sState.layoutScale != scale) {
        ComputeLayout(sState, display, scale);
    }
    const bool hidden = CVarGetInteger(CVAR_SETTING("TouchControls.Hidden"), 0) != 0;
    UpdateControls(sState, GatherFingers(display), hidden);

    bool menuDownNow = false;
    bool toggleDownNow = false;
    for (const auto& c : sState.controls) {
        menuDownNow |= c.isMenu && c.pressed;
        toggleDownNow |= c.isToggle && c.pressed;
    }
    if (sState.toggleDown && !toggleDownNow && !menuOpen) {
        CVarSetInteger(CVAR_SETTING("TouchControls.Hidden"), hidden ? 0 : 1);
    }
    sState.toggleDown = toggleDownNow;
    // Fires on release so the same tap can't also land on the menu that just opened.
    if (sState.menuDown && !menuDownNow && gui->GetMenu() != nullptr) {
        gui->GetMenu()->ToggleVisibility();
    }
    sState.menuDown = menuDownNow;

    if (menuOpen || hidden) {
        ReleaseAll(sState);
        return;
    }

    OSContPad* pad = static_cast<OSContPad*>(pads);
    for (const auto& c : sState.controls) {
        switch (c.kind) {
            case Kind::Button:
                if (c.pressed) {
                    pad[0].button |= c.mask;
                }
                break;
            case Kind::Face:
            case Kind::Cross:
                pad[0].button |= c.down;
                break;
            case Kind::Analog:
                if (!c.tracked.empty()) {
                    pad[0].stick_x = static_cast<int8_t>(c.stick.x * kStickMax);
                    pad[0].stick_y = static_cast<int8_t>(c.stick.y * kStickMax);
                }
                break;
        }
    }
}

void TouchControlsOverlay::Draw() {
    if (!sState.active || !Enabled()) {
        return;
    }

    const bool hidden = CVarGetInteger(CVAR_SETTING("TouchControls.Hidden"), 0) != 0;
    const float opacity =
        std::clamp(CVarGetFloat(CVAR_SETTING("TouchControls.Opacity"), 0.8f), 0.1f, 1.0f) * (hidden ? 0.5f : 1.0f);
    const float dp = sState.dp;
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImFont* font = ImGui::GetFont();

    const auto color = [opacity](float luminosity, float alpha) {
        const int v = static_cast<int>(luminosity * 255.0f);
        return IM_COL32(v, v, v, static_cast<int>(std::min(alpha * opacity, 1.0f) * 255.0f));
    };
    const auto text = [&](const ImVec2& center, float size, const char* s, ImU32 col) {
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
        drawList->AddText(font, size, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), col, s);
    };
    const auto glass = [&](const ImVec2& center, float radius, float luminosity, float fillAlpha, float shadowAlpha) {
        constexpr int kShadowSteps = 3;
        for (int i = kShadowSteps; i >= 1; i--) {
            drawList->AddCircleFilled(center, radius + 4.0f * dp * i / kShadowSteps,
                                      color(0.0f, shadowAlpha / kShadowSteps));
        }
        drawList->AddCircleFilled(center, radius, color(luminosity, fillAlpha));
    };
    const auto background = [&](const ImVec2& center, float diameter) {
        glass(center, diameter * 0.5f, 1.0f, 0.10f, 0.10f);
    };
    const auto composite = [&](const ImVec2& center, float diameter, bool pressed) {
        glass(center, diameter * 0.5f, pressed ? 0.0f : 1.0f, 0.125f, 0.05f);
    };
    const auto foreground = [&](const ImVec2& center, float slot, bool pressed) {
        const float diameter = slot - 16.0f * dp;
        glass(center, diameter * 0.5f, pressed ? 0.0f : 1.0f, 0.5f, 0.05f);
        return diameter;
    };
    const auto icons = [&](bool pressed) { return color(pressed ? 1.0f : 0.0f, 0.5f); };

    for (const auto& c : sState.controls) {
        if (hidden && !c.isToggle) {
            continue;
        }
        switch (c.kind) {
            case Kind::Button: {
                const bool pressed = c.isMenu ? sState.menuDown : c.isToggle ? sState.toggleDown : c.pressed;
                background(c.center, c.size);
                const float d = foreground(c.center, c.size, pressed);
                if (c.label != nullptr) {
                    text(c.center, d * 0.5f, c.label, icons(pressed));
                } else if (c.icon == Icon::Play) {
                    DrawTriangle(drawList, c.center, d * 0.5f, 0.0f, icons(pressed));
                } else if (c.icon == Icon::Menu) {
                    DrawMenuIcon(drawList, c.center, d * 0.5f, icons(pressed));
                } else if (c.icon == Icon::Dots) {
                    DrawDotsIcon(drawList, c.center, d * 0.5f, icons(pressed));
                }
                break;
            }
            case Kind::Analog: {
                background(c.center, c.size * 0.8f);
                const ImVec2 knob(c.center.x + c.stick.x * c.size * 0.25f, c.center.y - c.stick.y * c.size * 0.25f);
                foreground(knob, c.size * 0.66f, !c.tracked.empty());
                break;
            }
            case Kind::Face:
            case Kind::Cross: {
                background(c.center, c.size);
                for (size_t i = 0; i < c.anchors.size(); i++) {
                    const Anchor& a = c.anchors[i];
                    const float slot = c.size * a.size;
                    const float travel = (c.size - slot) * 0.5f;
                    const ImVec2 pos(c.center.x + a.pos.x * travel, c.center.y + a.pos.y * travel);
                    const bool pressed = (c.down & a.mask) == a.mask;
                    if (i >= c.primaryCount) {
                        composite(pos, slot, pressed);
                        continue;
                    }
                    const float d = foreground(pos, slot, pressed);
                    if (c.kind == Kind::Face) {
                        text(pos, d * 0.5f, c.labels[i], icons(pressed));
                    } else if (c.triangles) {
                        DrawTriangle(drawList, pos, d * 0.5f, std::atan2(a.pos.y, a.pos.x), icons(pressed));
                    } else {
                        DrawChevron(drawList, pos, d * 0.5f, std::atan2(a.pos.y, a.pos.x), icons(pressed));
                    }
                }
                break;
            }
        }
    }
}

#endif // __ANDROID__
