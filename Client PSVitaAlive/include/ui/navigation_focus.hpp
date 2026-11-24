#pragma once

namespace psvitaalive::ui {

enum class FocusZone { Catalog, Header, Tabs, Footer };
enum class FocusDirection { Left, Right, Up, Down };

// Catalog selection is owned by the existing grid/detail navigation. Chrome
// focus never replaces it, so a round-trip preserves the selected application.
class NavigationFocus {
public:
    FocusZone zone = FocusZone::Catalog;
    int index = 0;
    bool visible = true;

    void touch() { visible = false; catalog(); }
    void controller() { visible = true; }
    void catalog() { zone = FocusZone::Catalog; index = 0; }

    // Header indices are stable: search, clear search (conditional), filters.
    static constexpr bool headerAvailable(int i, bool clear, bool filters) {
        return i == 0 || (i == 1 && clear) || (i == 2 && filters);
    }
    void normalize(bool clear, bool filters, int tabs) {
        if (zone == FocusZone::Header && !headerAvailable(index, clear, filters))
            index = 0;
        if (zone == FocusZone::Tabs && (index < 0 || index >= tabs))
            index = 0;
        if (zone == FocusZone::Footer && (index < 0 || index > 2))
            index = 0;
    }
    void header() { zone = FocusZone::Header; index = 0; }
    void tab(int active) { zone = FocusZone::Tabs; index = active; }
    void footer(int selected = 0) { zone = FocusZone::Footer; index = selected; }

    void cycle() {
        if (zone == FocusZone::Catalog) header();
        else if (zone == FocusZone::Header || zone == FocusZone::Tabs) footer();
        else catalog();
    }

    // Only called for chrome; the catalog's existing movement stays intact.
    void move(FocusDirection d, bool clear, bool filters, int activeTab, int tabs) {
        normalize(clear, filters, tabs);
        switch (zone) {
        case FocusZone::Header:
            if (d == FocusDirection::Down) tab(activeTab);
            else if (d == FocusDirection::Left || d == FocusDirection::Right) {
                const int step = d == FocusDirection::Left ? -1 : 1;
                for (int next = index + step; next >= 0 && next <= 2; next += step)
                    if (headerAvailable(next, clear, filters)) { index = next; break; }
            }
            break;
        case FocusZone::Tabs:
            if (d == FocusDirection::Up) header();
            else if (d == FocusDirection::Down) catalog();
            else if (d == FocusDirection::Left && index > 0) --index;
            else if (d == FocusDirection::Right && index + 1 < tabs) ++index;
            break;
        case FocusZone::Footer:
            if (d == FocusDirection::Up) catalog();
            else if (d == FocusDirection::Left && index > 0) --index;
            else if (d == FocusDirection::Right && index < 2) ++index;
            break;
        case FocusZone::Catalog: break;
        }
    }
};

} // namespace psvitaalive::ui
