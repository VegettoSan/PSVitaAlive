#pragma once
#include <cstdint>

namespace psvitaalive::ui {

// One physical button history, shared across screen transitions.
// Invalid controller reads must not look like a button release.
class ControllerButtonEdges {
public:
    uint32_t sample(uint32_t buttons, bool validSample) {
        if (!validSample) return 0;
        const uint32_t pressed = buttons & ~previous_;
        previous_ = buttons;
        return pressed;
    }

private:
    uint32_t previous_ = 0;
};

} // namespace psvitaalive::ui
