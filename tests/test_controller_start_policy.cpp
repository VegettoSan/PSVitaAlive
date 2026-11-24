#include "ui/controller_start_policy.hpp"

#include <cassert>

int main() {
    using psvitaalive::ui::ControllerStartGate;
    constexpr unsigned start = 0x8u;
    constexpr unsigned other = 0x100u;
    ControllerStartGate gate;
    assert(gate.filter(start, start, true) == 0);
    assert(gate.filter(start | other, start, true) == other);
    assert(gate.filter(0, start, true) == 0);
    assert(gate.filter(start, start, true) == 0);
    gate.finishStartup();
    // Held at the boundary: no synthetic press and failed reads do not arm.
    assert(gate.filter(start, start, true) == 0);
    assert(gate.filter(0, start, false) == 0);
    assert(gate.filter(start | other, start, true) == other);
    assert(gate.filter(other, start, true) == other);
    assert(gate.filter(start | other, start, true) == (start | other));
    gate.finishStartup(); // repeated notifications must not reset the gate
    assert(gate.filter(start, start, true) == start);
    assert(gate.filter(start, start, false) == 0);

    ControllerStartGate released;
    released.finishStartup();
    assert(released.filter(0, start, true) == 0);
    assert(released.filter(start, start, true) == start);
}
