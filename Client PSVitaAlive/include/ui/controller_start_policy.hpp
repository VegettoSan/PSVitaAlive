#pragma once

namespace psvitaalive::ui {

// UI-thread-owned gate. A START held during startup must not become a
// synthetic press when the startup sequence finishes.
class ControllerStartGate {
public:
    void finishStartup() { startupFinished_ = true; }

    unsigned filter(unsigned buttons, unsigned startMask, bool validSample) {
        if (!startupFinished_ || !validSample)
            return buttons & ~startMask;
        if (awaitRelease_) {
            if ((buttons & startMask) == 0) awaitRelease_ = false;
            return buttons & ~startMask;
        }
        return buttons;
    }

private:
    bool startupFinished_ = false;
    bool awaitRelease_ = true;
};

} // namespace psvitaalive::ui
