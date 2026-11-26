#include "ui/navigation_focus.hpp"
#include <cassert>
#include <initializer_list>
using namespace psvitaalive::ui;
using Z = FocusZone;
using D = FocusDirection;

int main() {
    NavigationFocus f;
    assert(f.zone == Z::Catalog && f.visible);
    f.cycle(); assert(f.zone == Z::Footer && f.index == 0);
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
    f.cycle(); assert(f.zone == Z::Catalog && f.index == 0);
    f.cycle(); assert(f.zone == Z::Footer && f.index == 0);
    f.move(D::Right, false, false, 0, 4); assert(f.index == 1);
    f.move(D::Right, false, false, 0, 4); assert(f.index == 2);
    f.move(D::Right, false, false, 0, 4); assert(f.index == 3);
    f.move(D::Right, false, false, 0, 4); assert(f.index == 3);
    f.move(D::Up, false, false, 0, 4); assert(f.zone == Z::Catalog);
    f.tab(3); f.move(D::Down, false, false, 0, 4); assert(f.zone == Z::Catalog);
    f.footer(2); f.cycle(); assert(f.zone == Z::Header && f.index == 0);
    f.cycle(); assert(f.zone == Z::Catalog);
    f.tab(3); f.cycle(); assert(f.zone == Z::Catalog && f.index == 0);
    f.cycle(); assert(f.zone == Z::Footer && f.index == 0);
    f.move(D::Up, false, false, 0, 4); assert(f.zone == Z::Catalog);
    f.cycle(); assert(f.zone == Z::Footer); // follow actual zone, not a counter
    f.header(); f.touch(); assert(!f.visible && f.zone == Z::Catalog);
    f.controller(); assert(f.visible && f.zone == Z::Catalog);
    f.footer(2); f.touch(); assert(!f.visible && f.zone == Z::Catalog);
    f.controller(); f.cycle(); assert(f.visible && f.zone == Z::Footer);
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
    // Queue navigation accepts a combined D-pad/stick direction, without
    // double movement, repeated confirmation, or replay after a modal.
    QueueVerticalNavigation q;
    assert(q.sample(false,true,1000)==1);
    assert(q.sample(false,true,330999)==0);
    assert(q.sample(false,true,331000)==1);
    assert(q.sample(false,true,460999)==0);
    assert(q.sample(false,true,461000)==1);
    assert(q.sample(true,false,462000)==-1);
    assert(q.sample(true,true,463000)==0);
    assert(q.sample(false,true,464000)==1);
    assert(q.sample(false,true,465000,true)==0);
    assert(q.sample(false,true,900000)==0);
    assert(q.sample(false,false,901000)==0);
    assert(q.sample(false,true,902000)==1);
    assert(q.sample(false,true,2000000)==1); // one repeat after a stall
    assert(q.sample(false,true,2000001)==0);
    q.reset();
    assert(q.sample(true,false,2000002)==-1);
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
