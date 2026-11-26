#pragma once
#include <psp2/ctrl.h>
#include "ui/controller_start_policy.hpp"
#include "ui/controller_button_edges.hpp"

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

inline uint32_t clientStickDirections(const SceCtrlData& pad) {
    uint32_t nav = 0;
    if (pad.lx <= 64) nav |= SCE_CTRL_LEFT;
    else if (pad.lx >= 192) nav |= SCE_CTRL_RIGHT;
    if (pad.ly <= 64) nav |= SCE_CTRL_UP;
    else if (pad.ly >= 192) nav |= SCE_CTRL_DOWN;
    return nav;
}

// The UI thread has one action history, including the keyboard and modal
// dialogs. Raw polling remains available for non-action observation only.
inline ControllerButtonEdges clientButtonEdges;
inline uint32_t sampleClientControllerActions(const SceCtrlData& pad, bool valid) {
    return clientButtonEdges.sample(pad.buttons | clientStickDirections(pad), valid);
}
inline int readClientControllerActions(SceCtrlData& pad, uint32_t& pressed) {
    const int result = peekClientController(pad);
    pressed = sampleClientControllerActions(pad, result > 0);
    return result;
}
inline void discardClientControllerActions() {
    SceCtrlData pad{};
    uint32_t ignored = 0;
    readClientControllerActions(pad, ignored);
}

} // namespace psvitaalive::ui
