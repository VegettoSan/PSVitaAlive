#include "ui/loading_input_policy.hpp"
#include <cassert>

int main() {
    using namespace psvitaalive::ui;
    // Startup before loading flags, loading, and the visible fade-out.
    assert(loadingInputBlocked(true, false, false, false));
    assert(loadingInputBlocked(false, true, false, false));
    assert(loadingInputBlocked(false, false, true, false));
    assert(!loadingInputBlocked(false, false, false, false));
    // First-run theme, news, and plugin dialogs own input above the splash.
    assert(!loadingInputBlocked(true, true, true, true));
    assert(!loadingInputBlocked(false, true, true, true));
    assert(!loadingInputBlocked(false, false, true, true));

    LoadingInputGate gate;
    gate.update(true);
    assert(gate.blocked());
    assert(!gate.controllerReady(true, true));
    assert(!gate.touchReady(true, true));
    gate.update(false);
    assert(!gate.blocked());
    assert(!gate.controllerReady(false, true));
    assert(!gate.controllerReady(true, false)); // held button or analog
    assert(!gate.touchReady(true, false)); // finger held across splash
    assert(!gate.touchReady(false, true)); // failed sample is not release
    assert(gate.controllerReady(true, true));
    assert(!gate.controllerPending());
    assert(gate.controllerReady(true, false));
    assert(gate.touchPending()); // devices recover independently
    assert(gate.touchReady(true, true));
    assert(gate.touchReady(true, false));
    // Switching catalog must re-arm both guards.
    gate.update(true);
    gate.update(false);
    assert(gate.controllerPending() && gate.touchPending());
    assert(!gate.controllerReady(true, false));
    assert(!gate.touchReady(true, false));
    assert(gate.touchReady(true, true));
    assert(gate.controllerReady(true, true));
    gate.update(false); // ordinary frames do not re-arm
    assert(!gate.controllerPending() && !gate.touchPending());
}
