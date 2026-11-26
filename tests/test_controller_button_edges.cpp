#include "ui/controller_button_edges.hpp"
#include <cassert>

int main() {
    using psvitaalive::ui::ControllerButtonEdges;
    constexpr uint32_t cross = 0x4000u;
    constexpr uint32_t circle = 0x2000u;
    constexpr uint32_t triangle = 0x1000u;
    constexpr uint32_t down = 0x40u;
    static_assert(sizeof(ControllerButtonEdges) == sizeof(uint32_t));
    // Exhaust every possible button bit (including direction aliases).
    for (unsigned bit = 0; bit < 32; ++bit) {
        const uint32_t mask = uint32_t{1} << bit;
        ControllerButtonEdges all;
        assert(all.sample(mask, true) == mask);
        for (int i = 0; i < 120; ++i) assert(all.sample(mask, true) == 0);
        assert(all.sample(0, false) == 0);
        assert(all.sample(mask, true) == 0);
        assert(all.sample(0, true) == 0);
        assert(all.sample(mask, true) == mask);
    }
    ControllerButtonEdges edges;
    auto sample = [&](uint32_t buttons, bool valid = true) {
        return edges.sample(buttons, valid);
    };
    // Catalog -> storage -> installed data -> detail -> confirmation.
    assert(sample(0) == 0);
    assert(sample(cross) == cross);
    for (int i = 0; i < 120; ++i) assert(sample(cross) == 0);
    assert(sample(0, false) == 0); // failed read is not a release
    assert(sample(cross) == 0);
    assert(sample(0) == 0);
    assert(sample(cross) == cross);
    assert(sample(cross) == 0); // entering another screen cannot confirm deletion
    assert(sample(0) == 0);
    assert(sample(cross) == cross); // a deliberate second tap still works
    assert(sample(0) == 0);
    // Return one level per physical press, including rapid re-presses.
    for (int level = 0; level < 4; ++level) {
        assert(sample(circle) == circle);
        for (int frame = 0; frame < 60; ++frame) assert(sample(circle) == 0);
        assert(sample(0) == 0);
    }
    // Direction edges and independent buttons are preserved.
    assert(sample(down) == down);
    assert(sample(down | triangle) == triangle);
    assert(sample(down | triangle | cross) == cross);
    assert(sample(down | triangle | cross) == 0);
    assert(sample(down) == 0);
    assert(sample(down | cross) == cross);
}
