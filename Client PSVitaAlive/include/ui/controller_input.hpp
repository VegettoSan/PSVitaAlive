#pragma once
#include <psp2/ctrl.h>
#include "ui/controller_start_policy.hpp"

namespace psvitaalive::ui {

// All client controller polling runs on the UI thread. Only the startup
// phase suppresses START; normal input handlers retain their own shortcuts.
inline ControllerStartGate clientStartGate;
inline void finishClientControllerStartup() {
    clientStartGate.finishStartup();
}

inline int peekClientController(SceCtrlData& pad) {
    const int result = sceCtrlPeekBufferPositive(0, &pad, 1);
    pad.buttons = clientStartGate.filter(pad.buttons,
        static_cast<unsigned>(SCE_CTRL_START), result > 0);
    return result;
}

} // namespace psvitaalive::ui
