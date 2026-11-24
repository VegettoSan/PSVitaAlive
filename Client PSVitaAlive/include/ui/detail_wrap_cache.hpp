#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace psvitaalive::ui {

// UI-thread only. A result is borrowed until the next lookup or clear().
// Keep a small number of exact layouts, never one entry per catalog item.
class DetailWrapCache {
public:
    using Lines = std::vector<std::string>;
    static constexpr std::size_t capacity = 8;
    static constexpr std::size_t maxTextBytes = 8192;
    static constexpr std::size_t maxLines = 256;

    template<class Build>
    const Lines& get(const std::string& text, int width, bool bullets,
                     int fontScale, Build&& build) {
        // Oversized results are borrowed for one call only; do not retain them.
        if (scratch_.capacity()) Lines{}.swap(scratch_);
        if (text.empty()) return empty_;
        const bool eligible = text.size() <= maxTextBytes;
        if (eligible) {
            for (const auto& entry : entries_) {
                if (entry.valid && entry.width == width && entry.bullets == bullets &&
                    entry.fontScale == fontScale && entry.text == text)
                    return entry.lines;
            }
        }
        Lines lines = build();
        if (!eligible || lines.size() > maxLines) {
            scratch_ = std::move(lines);
            return scratch_;
        }
        auto& entry = entries_[next_];
        next_ = (next_ + 1) % capacity;
        entry = Entry{true, width, bullets, fontScale, text, std::move(lines)};
        return entry.lines;
    }

    // Call whenever the underlying font handle is replaced or released.
    void clear() {
        entries_ = {};
        Lines{}.swap(scratch_);
        next_ = 0;
    }

private:
    struct Entry {
        bool valid = false;
        int width = 0;
        bool bullets = false;
        int fontScale = 0;
        std::string text;
        Lines lines;
    };
    std::array<Entry, capacity> entries_{};
    Lines scratch_;
    const Lines empty_;
    std::size_t next_ = 0;
};

} // namespace psvitaalive::ui
