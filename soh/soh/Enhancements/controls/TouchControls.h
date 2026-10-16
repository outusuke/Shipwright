#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Merges the touch overlay into pad 0. `pads` is the OSContPad array from osContGetReadData().
void TouchControls_ApplyPad(void* pads);

#ifdef __cplusplus
}

#include <ship/window/gui/GuiWindow.h>

class TouchControlsOverlay final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void Draw() override;
    void InitElement() override {};
    void DrawElement() override {};
    void UpdateElement() override {};
};
#endif
