#include "ui/feedback_scroll_math.hpp"

#include <cassert>

int main() {
    using namespace psvitaalive::ui;
    constexpr int bodyHeight = 148;
    constexpr int firstBaseline = 30;
    constexpr int bottomInset = 12;
    constexpr int lineStep = 27;
    constexpr int visible = feedbackVisibleTextSpan(
        bodyHeight, firstBaseline, bottomInset, lineStep);
    static_assert(visible == 133, "Description visible span must match drawing");
    constexpr int lines = 12;
    constexpr int maxInner = lines * lineStep - visible;
    static_assert(firstBaseline + (lines - 1) * lineStep - maxInner ==
                  bodyHeight - bottomInset, "Last line must reach lower inset");

    auto p = feedbackApplyNestedScroll(0, 100, 0, 300, 60);
    assert(p.inner == 60 && p.outer == 0);
    p = feedbackApplyNestedScroll(p.inner, 100, p.outer, 300, 80);
    assert(p.inner == 100 && p.outer == 40);
    p = feedbackApplyNestedScroll(p.inner, 100, p.outer, 300, -120);
    assert(p.inner == 0 && p.outer == 20);
    p = feedbackApplyNestedScroll(0, 0, 20, 300, 50);
    assert(p.inner == 0 && p.outer == 70);
    p = feedbackApplyNestedScroll(100, 100, 280, 300, 500);
    assert(p.inner == 100 && p.outer == 300);
    p = feedbackApplyNestedScroll(0, 100, 0, 300, -500);
    assert(p.inner == 0 && p.outer == 0);
}
