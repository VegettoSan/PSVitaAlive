#pragma once
// Match screenshot GPU residency to the exact visible rectangle.
// A row is retained while even one pixel intersects the detail viewport.
namespace psvitaalive { namespace ui {
inline bool detailScreenshotIntersectsViewport(int rowTop, int rowHeight,
                                               int viewportTop, int viewportBottom) {
    return rowHeight > 0 && viewportBottom > viewportTop &&
           rowTop < viewportBottom && rowTop + rowHeight > viewportTop;
}
}} // namespace psvitaalive::ui
