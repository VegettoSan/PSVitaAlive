#pragma once
#include <psp2/ctrl.h>

namespace psvitaalive::ui {

// START is intentionally unused throughout the client. Filter it before
// edge detection, including any-button wakeups and startup rendering.
inline int peekClientController(SceCtrlData& pad) {
    const int result = sceCtrlPeekBufferPositive(0, &pad, 1);
    pad.buttons &= ~static_cast<unsigned>(SCE_CTRL_START);
    return result;
}

} // namespace psvitaalive::ui
