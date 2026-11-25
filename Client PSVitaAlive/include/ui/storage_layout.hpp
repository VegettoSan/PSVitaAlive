#pragma once
#include <cstdint>
namespace psvitaalive::ui {
struct StorageLayout {
    static constexpr int width = 864, height = 512;
    static constexpr int listOffset = 126, viewportHeight = 288;
    static constexpr int gap = 10;
    static constexpr int buttonOffset = 468, buttonHeight = 34;
    static constexpr int closeWidth = 176, cleanWidth = 212;
    static constexpr int margin = 21;
    // A smooth back-and-forth activity marker, not a completion estimate.
    static constexpr int loadingSegmentWidth(int width) {
        return width <= 0 ? 0 : (width < 4 ? width : width / 4);
    }
    static constexpr int loadingSegmentOffset(int width, std::uint64_t ms) {
        const int travel = width - loadingSegmentWidth(width);
        if (travel <= 0) return 0;
        const auto tick = ms % 1800u;
        const auto step = tick <= 900u ? tick : 1800u - tick;
        return static_cast<int>(static_cast<std::uint64_t>(travel)*step/900u);
    }
    static constexpr int rowHeight(int kind, bool savedZip) {
        return kind == 4 ? (savedZip ? 96 : 64) : 80;
    }
    static constexpr bool fullyVisible(int offset, int height, int scroll) {
        return offset >= scroll && offset + height <= scroll + viewportHeight;
    }
    static constexpr bool rowHit(int y, int offset, int height, int scroll) {
        return fullyVisible(offset, height, scroll) &&
            y >= offset - scroll && y < offset - scroll + height - gap;
    }
};
}
