#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Applies a pending Select-button menu toggle; call from the game thread.
void AndroidBridge_PollMenuToggle(void);

#ifdef __cplusplus
}
#endif
