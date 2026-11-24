#pragma once

namespace psvitaalive::ui {

// Keep the current column when possible; otherwise use the last available
// card of the next row. Never move horizontally within the final row.
inline constexpr int moveCatalogGridVertical(int focus, int count, int direction) {
    if (count <= 0) return 0;
    if (focus < 0) focus = 0;
    if (focus >= count) focus = count - 1;
    constexpr int columns = 3;
    if (direction < 0)
        return focus >= columns ? focus - columns : focus;
    if (direction > 0 && focus / columns < (count - 1) / columns) {
        const int remaining = count - 1 - focus;
        return remaining >= columns ? focus + columns : count - 1;
    }
    return focus;
}

} // namespace psvitaalive::ui
