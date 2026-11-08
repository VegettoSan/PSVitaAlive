#include "ui/detail_screenshot_visibility.hpp"
#include <cassert>
using psvitaalive::ui::detailScreenshotIntersectsViewport;
int main() {
    // Actual detail body bounds at 960x544: 206..480, screenshot = 232px.
    constexpr int top=206,bottom=480,height=232,step=250;
    assert(detailScreenshotIntersectsViewport(300,height,top,bottom));
    assert(detailScreenshotIntersectsViewport(top,height,top,bottom));
    assert(detailScreenshotIntersectsViewport(top-height+1,height,top,bottom));
    assert(!detailScreenshotIntersectsViewport(top-height,height,top,bottom));
    assert(detailScreenshotIntersectsViewport(bottom-1,height,top,bottom));
    assert(!detailScreenshotIntersectsViewport(bottom,height,top,bottom));
    // Scrolling changes the actual rendered position, not the number of links.
    constexpr int contentFirstRow=812;
    for (int scroll=580;scroll<=837;scroll+=13) {
        const int rowY=top+contentFirstRow-scroll;
        const bool first=detailScreenshotIntersectsViewport(rowY,height,top,bottom);
        const bool second=detailScreenshotIntersectsViewport(rowY+step,height,top,bottom);
        assert(first || second); // At least one real row still intersects.
    }
    // Different catalog sections/layout lengths must not override the viewport.
    assert(detailScreenshotIntersectsViewport(320,height,top,bottom));
    assert(!detailScreenshotIntersectsViewport(500,height,top,bottom));
    assert(!detailScreenshotIntersectsViewport(220,0,top,bottom));
    return 0;
}
