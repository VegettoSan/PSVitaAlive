#pragma once

#include "ui/ui_font.hpp"
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace psvitaalive::ui {

// The keyboard never uses Sony's IME, PAF or CommonDialog. Single UI thread.
struct KeyboardPalette {
    unsigned bg = 0;
    unsigned surface = 0;
    unsigned surface2 = 0;
    unsigned border = 0;
    unsigned text = 0;
    unsigned dim = 0;
    unsigned accent = 0;
};

class SearchKeyboard {
public:
    bool open(const std::string& initial, const std::string& title);
    bool active() const { return active_; }
    void update();
    /** Render over the already-drawn catalog in a single vita2d frame. */
    void drawOverlay(const UiFont* font, const KeyboardPalette& colors) const;
    bool takeResult(std::string& text, bool& accepted);

private:
    enum class Action { Character, Shift, Numbers, More, Letters, Space,
                        Backspace, Confirm, Cancel };
    struct Definition {
        const char* label;
        char character;
        Action action;
        int weight;
    };
    struct Key {
        std::string label;
        char character;
        Action action;
        int x, y, width, height, row;
    };

    void rebuild();
    void addRow(std::initializer_list<Definition> defs, int row);
    void navigate(int direction);
    void activate(int index, uint64_t now);
    void eraseOne();
    void finish(bool accepted, uint64_t now);
    int hitTest(int x, int y) const;

    std::vector<Key> keys_;
    std::string input_;
    std::string title_;
    bool active_ = false;
    bool closing_ = false;
    bool accepted_ = false;
    bool ready_ = false;
    bool numbers_ = false;
    bool more_ = false;
    bool shift_ = false;
    bool touchWasDown_ = false;
    bool eraseHeld_ = false;
    unsigned previousButtons_ = 0;
    unsigned lastNav_ = 0;
    int focused_ = 0;
    int touchBackspace_ = -1;
    uint64_t openedAtUs_ = 0;
    uint64_t closeAtUs_ = 0;
    uint64_t nextNavUs_ = 0;
    uint64_t nextEraseUs_ = 0;
};

} // namespace psvitaalive::ui
