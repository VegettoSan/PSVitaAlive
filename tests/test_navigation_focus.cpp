#include "ui/navigation_focus.hpp"
#include <cassert>
#include <initializer_list>
using namespace psvitaalive::ui;
using Z = FocusZone;
using D = FocusDirection;

int main() {
    NavigationFocus f;
    assert(f.zone == Z::Catalog && f.visible);
    f.cycle(); assert(f.zone == Z::Header && f.index == 0);
    f.move(D::Right, false, false, 2, 4); assert(f.index == 0);
    f.move(D::Right, false, true, 2, 4); assert(f.index == 2);
    f.move(D::Left, true, true, 2, 4); assert(f.index == 1);
    f.normalize(false, true, 4); assert(f.index == 0);
    f.move(D::Right, true, false, 2, 4); assert(f.index == 1);
    f.move(D::Right, true, false, 2, 4); assert(f.index == 1);
    f.move(D::Down, true, false, 2, 4); assert(f.zone == Z::Tabs && f.index == 2);
    f.move(D::Right, true, false, 2, 4); assert(f.index == 3);
    f.move(D::Right, true, false, 2, 4); assert(f.index == 3);
    f.move(D::Left, true, false, 2, 4); assert(f.index == 2);
    f.move(D::Up, true, false, 2, 4); assert(f.zone == Z::Header && f.index == 0);
    f.cycle(); assert(f.zone == Z::Footer && f.index == 0);
    f.move(D::Right, false, false, 0, 4); assert(f.index == 1);
    f.move(D::Right, false, false, 0, 4); assert(f.index == 2);
    f.move(D::Right, false, false, 0, 4); assert(f.index == 3);
    f.move(D::Right, false, false, 0, 4); assert(f.index == 3);
    f.move(D::Up, false, false, 0, 4); assert(f.zone == Z::Catalog);
    f.tab(3); f.move(D::Down, false, false, 0, 4); assert(f.zone == Z::Catalog);
    f.footer(2); f.cycle(); assert(f.zone == Z::Catalog);
    f.header(); f.touch(); assert(!f.visible && f.zone == Z::Catalog);
    f.controller(); assert(f.visible && f.zone == Z::Catalog);
    f.footer(2); f.touch(); assert(!f.visible && f.zone == Z::Catalog);
    f.controller(); f.cycle(); assert(f.visible && f.zone == Z::Header);
    // Logical selection survives, but only the active zone owns the card
    // highlight. Touch returns to content even with chrome hidden.
    f.catalog(); assert(f.catalogCardFocused(true));
    assert(!f.catalogCardFocused(false));
    f.header(); assert(!f.catalogCardFocused(true));
    f.tab(2); assert(!f.catalogCardFocused(true));
    f.footer(2); assert(!f.catalogCardFocused(true));
    f.touch(); assert(f.catalogCardFocused(true) && !f.visible);
    f.controller(); assert(f.catalogCardFocused(true));
    f.cycle(); assert(!f.catalogCardFocused(true));
    // Every combination of dynamic header controls: no invisible focus.
    for (bool clear : {false, true}) {
        for (bool filters : {false, true}) {
            f.header();
            for (int i = 0; i < 8; ++i) {
                f.move(D::Right, clear, filters, 0, 4);
                assert(NavigationFocus::headerAvailable(f.index, clear, filters));
            }
            for (int i = 0; i < 8; ++i) {
                f.move(D::Left, clear, filters, 0, 4);
                assert(NavigationFocus::headerAvailable(f.index, clear, filters));
            }
            assert(f.index == 0);
        }
    }
}
