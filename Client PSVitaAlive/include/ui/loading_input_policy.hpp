#pragma once

namespace psvitaalive::ui {

// Interactive dialogs are drawn above the splash and own their input.
// The startup sequence and visible full-screen catalog loads otherwise
// suppress input until the first neutral sample from each input device.
inline constexpr bool loadingInputBlocked(bool startupPending, bool catalogLoading,
                                          bool splashVisible, bool interactiveOverlay) {
    return !interactiveOverlay && (startupPending || catalogLoading || splashVisible);
}

class LoadingInputGate {
public:
    void update(bool blocked) {
        blocked_ = blocked;
        if (blocked) {
            controllerPending_ = true;
            touchPending_ = true;
        }
    }
    bool blocked() const { return blocked_; }
    bool controllerPending() const { return controllerPending_; }
    bool touchPending() const { return touchPending_; }

    bool controllerReady(bool validSample, bool neutral) {
        if (blocked_) return false;
        if (controllerPending_ && validSample && neutral)
            controllerPending_ = false;
        return !controllerPending_;
    }
    bool touchReady(bool validSample, bool released) {
        if (blocked_) return false;
        if (touchPending_ && validSample && released)
            touchPending_ = false;
        return !touchPending_;
    }

private:
    bool blocked_ = true;
    bool controllerPending_ = true;
    bool touchPending_ = true;
};

} // namespace psvitaalive::ui
