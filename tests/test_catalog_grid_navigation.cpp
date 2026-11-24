#include "ui/catalog_grid_navigation.hpp"

#include <cassert>

int main() {
    using psvitaalive::ui::moveCatalogGridVertical;
    // Last row with one card: both missing columns reach it.
    assert(moveCatalogGridVertical(7, 10, 1) == 9);
    assert(moveCatalogGridVertical(8, 10, 1) == 9);
    // Last row with two cards: column three reaches column two.
    assert(moveCatalogGridVertical(8, 11, 1) == 10);
    assert(moveCatalogGridVertical(7, 11, 1) == 10);
    assert(moveCatalogGridVertical(6, 11, 1) == 9);
    assert(moveCatalogGridVertical(8, 12, 1) == 11);
    assert(moveCatalogGridVertical(10, 11, -1) == 7);
    assert(moveCatalogGridVertical(9, 10, -1) == 6);
    assert(moveCatalogGridVertical(9, 11, 1) == 9);
    assert(moveCatalogGridVertical(1, 2, 1) == 1);
    assert(moveCatalogGridVertical(2, 3, -1) == 2);
    assert(moveCatalogGridVertical(0, 0, 1) == 0);
    assert(moveCatalogGridVertical(0, 1, -1) == 0);
    assert(moveCatalogGridVertical(-1, 4, 1) == 3);
    assert(moveCatalogGridVertical(20, 4, 1) == 3);
    assert(moveCatalogGridVertical(4, 10, 0) == 4);

    // All valid starting cards, all final-row shapes and filtered counts.
    for (int count = 1; count <= 200; ++count) {
        for (int focus = 0; focus < count; ++focus) {
            const int down = moveCatalogGridVertical(focus, count, 1);
            const int up = moveCatalogGridVertical(focus, count, -1);
            assert(down >= 0 && down < count);
            assert(up >= 0 && up < count);
            const int lastRow = (count - 1) / 3;
            const int row = focus / 3;
            if (row < lastRow) {
                const int rowLast = (row + 2) * 3 - 1;
                const int availableLast = rowLast < count ? rowLast : count - 1;
                const int sameColumn = focus + 3;
                assert(down == (sameColumn < availableLast ? sameColumn : availableLast));
                assert(down / 3 == row + 1);
            } else {
                assert(down == focus);
            }
            assert(up == (row > 0 ? focus - 3 : focus));
        }
    }
}
