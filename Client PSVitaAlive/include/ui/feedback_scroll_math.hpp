#pragma once

#include <algorithm>

namespace psvitaalive::ui {

// Let a nested text area consume a drag first; forward any unused distance
// to its containing scrollable panel. Both positions remain within bounds.
struct FeedbackNestedScroll {
    int inner;
    int outer;
};

inline FeedbackNestedScroll feedbackApplyNestedScroll(
    int inner, int innerMax, int outer, int outerMax, int delta) {
    innerMax = std::max(0, innerMax);
    outerMax = std::max(0, outerMax);
    inner = std::max(0, std::min(inner, innerMax));
    outer = std::max(0, std::min(outer, outerMax));
    const int nextInner = std::max(0, std::min(inner + delta, innerMax));
    const int residual = delta - (nextInner - inner);
    const int nextOuter = std::max(0, std::min(outer + residual, outerMax));
    return {nextInner, nextOuter};
}

// The last baseline must stop above the bottom inset; the first one starts
// at firstBaseline and successive baselines advance by lineHeight.
constexpr int feedbackVisibleTextSpan(int fieldHeight, int firstBaseline,
                                      int bottomInset, int lineHeight) {
    return fieldHeight - firstBaseline - bottomInset + lineHeight;
}

} // namespace psvitaalive::ui
