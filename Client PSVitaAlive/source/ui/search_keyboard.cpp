#include "ui/search_keyboard.hpp"
#include "localization/localization.hpp"

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace psvitaalive::ui {
namespace {
constexpr int kScreenW = 960;
constexpr int kScreenH = 544;
constexpr int kPanelX = 46;
constexpr int kPanelY = 34;
constexpr int kPanelW = 868;
constexpr int kPanelH = 482;
constexpr int kRowW = 708;
constexpr int kActionX = 801;
constexpr int kActionW = 91;
constexpr int kRowX = (kScreenW - kRowW) / 2;
constexpr int kRowTop = 178;
constexpr int kRowHeight = 57;
constexpr int kRowGap = 9;
constexpr int kKeyGap = 7;
constexpr uint64_t kAnimationUs = 230000ULL;
constexpr size_t kMaxInputBytes = 128;

uint64_t timeUs() { return sceKernelGetSystemTimeWide(); }
float smooth(float t) {
    t = std::max(0.0f, std::min(1.0f, t));
    return t * t * (3.f - 2.f*t);
}
void centered(const UiFont* font, int x, int y, int w, unsigned color,
              float scale, const char* label) {
    const int tw = uiTextWidth(font, scale, label);
    uiDrawText(font, x + (w - tw) / 2, y, color, scale, label);
}
} // namespace

bool SearchKeyboard::open(const std::string& initial, const std::string& title) {
    if (active_) return false;
    input_ = initial.substr(0, kMaxInputBytes);
    title_ = title.empty() ? "Search catalog" : title;
    numbers_ = more_ = shift_ = false;
    closing_ = accepted_ = ready_ = eraseHeld_ = false;
    focused_ = 0;
    touchBackspace_ = -1;
    openedAtUs_ = timeUs();
    closeAtUs_ = nextNavUs_ = nextEraseUs_ = 0;
    previousButtons_ = lastNav_ = 0;
    SceCtrlData pad{};
    if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) previousButtons_ = pad.buttons;
    SceTouchData touch{};
    touchWasDown_ = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0 &&
                    touch.reportNum > 0;
    active_ = true;
    rebuild();
    return true;
}

void SearchKeyboard::addRow(std::initializer_list<Definition> defs, int row) {
    int weights = 0;
    for (const Definition& d : defs) weights += d.weight;
    const int total = kRowW - kKeyGap * ((int)defs.size() - 1);
    int x = kRowX, i = 0;
    for (const Definition& d : defs) {
        const int w = (++i == (int)defs.size()) ? kRowX + kRowW - x :
                      total * d.weight / weights;
        keys_.push_back({d.label, d.character, d.action,
                         x, kRowTop + row * (kRowHeight + kRowGap),
                         w, kRowHeight, row});
        x += w + kKeyGap;
    }
}

void SearchKeyboard::rebuild() {
    keys_.clear();
    using A = Action;
    if (!numbers_) {
        addRow({{"Q",'q',A::Character,1},{"W",'w',A::Character,1},
                {"E",'e',A::Character,1},{"R",'r',A::Character,1},
                {"T",'t',A::Character,1},{"Y",'y',A::Character,1},
                {"U",'u',A::Character,1},{"I",'i',A::Character,1},
                {"O",'o',A::Character,1},{"P",'p',A::Character,1}},0);
        addRow({{"A",'a',A::Character,1},{"S",'s',A::Character,1},
                {"D",'d',A::Character,1},{"F",'f',A::Character,1},
                {"G",'g',A::Character,1},{"H",'h',A::Character,1},
                {"J",'j',A::Character,1},{"K",'k',A::Character,1},
                {"L",'l',A::Character,1}},1);
        addRow({{"Z",'z',A::Character,1},{"X",'x',A::Character,1},
                {"C",'c',A::Character,1},{"V",'v',A::Character,1},
                {"B",'b',A::Character,1},{"N",'n',A::Character,1},
                {"M",'m',A::Character,1},{"?",'?',A::Character,1},
                {"!",'!',A::Character,1}},2);
        addRow({{"123",0,A::Numbers,3},{"SPACE",0,A::Space,8},
                {".",'.',A::Character,2}},3);
    } else {
        addRow({{"1",'1',A::Character,1},{"2",'2',A::Character,1},
                {"3",'3',A::Character,1},{"4",'4',A::Character,1},
                {"5",'5',A::Character,1},{"6",'6',A::Character,1},
                {"7",'7',A::Character,1},{"8",'8',A::Character,1},
                {"9",'9',A::Character,1},{"0",'0',A::Character,1}},0);
        if (!more_) {
            addRow({{"!",'!',A::Character,1},{"@",'@',A::Character,1},
                    {"#",'#',A::Character,1},{"$",'$',A::Character,1},
                    {"%",'%',A::Character,1},{"&",'&',A::Character,1},
                    {"*",'*',A::Character,1},{"(",'(',A::Character,1},
                    {")",')',A::Character,1}},1);
            addRow({{":",':',A::Character,1},{"/",'/',A::Character,1},
                    {"\\",'\\',A::Character,1},{"_",'_',A::Character,1},
                    {"-",'-',A::Character,1},{"+",'+',A::Character,1},
                    {"=",'=',A::Character,1},{"?",'?',A::Character,1},
                    {".",'.',A::Character,1}},2);
        } else {
            addRow({{"[",'[',A::Character,1},{"]",']',A::Character,1},
                    {"{",'{',A::Character,1},{"}",'}',A::Character,1},
                    {"<",'<',A::Character,1},{">",'>',A::Character,1},
                    {"~",'~',A::Character,1},{"^",'^',A::Character,1},
                    {"|",'|',A::Character,1}},1);
            addRow({{";",';',A::Character,1},{"'",'\'',A::Character,1},
                    {"\\\"",'"',A::Character,1},{"`",'`',A::Character,1},
                    {",",',',A::Character,1},{"?",'?',A::Character,1},
                    {"/",'/',A::Character,1},{"@",'@',A::Character,1},
                    {"_", '_',A::Character,1}},2);
        }
        addRow({{"ABC",0,A::Letters,3},{"MORE",0,A::More,3},
                {"SPACE",0,A::Space,8},{".",'.',A::Character,2}},3);
    }
    // Dedicated right-hand action column, on both QWERTY and 123 pages.
    keys_.push_back({"SHIFT", 0, A::Shift, kActionX, kRowTop, kActionW, kRowHeight, 0});
    keys_.push_back({"DEL", 0, A::Backspace, kActionX, kRowTop + (kRowHeight+kRowGap),
                     kActionW, kRowHeight, 1});
    keys_.push_back({"CANCEL", 0, A::Cancel, kActionX, kRowTop + 2*(kRowHeight+kRowGap),
                     kActionW, kRowHeight, 2});
    keys_.push_back({"OK", 0, A::Confirm, kActionX, kRowTop + 3*(kRowHeight+kRowGap),
                     kActionW, kRowHeight, 3});
    if (focused_ >= (int)keys_.size()) focused_ = 0;
}

void SearchKeyboard::navigate(int direction) {
    if (keys_.empty()) return;
    const Key& selected = keys_[focused_];
    int best = focused_;
    int closest = 10000000;
    const int cx = selected.x + selected.width / 2;
    for (int i = 0; i < (int)keys_.size(); ++i) {
        const Key& candidate = keys_[i];
        if (direction == -1 || direction == 1) {
            if (candidate.row != selected.row) continue;
            if (direction == -1 && candidate.x >= selected.x) continue;
            if (direction == 1 && candidate.x <= selected.x) continue;
            const int delta = std::abs(candidate.x - selected.x);
            if (delta < closest) { closest = delta; best = i; }
        } else {
            if (candidate.row != selected.row + (direction == -2 ? -1 : 1)) continue;
            const int delta = std::abs(candidate.x + candidate.width/2 - cx);
            if (delta < closest) { closest = delta; best = i; }
        }
    }
    focused_ = best;
}

void SearchKeyboard::eraseOne() {
    if (input_.empty()) return;
    // Preserve an existing multi-byte UTF-8 query when deleting from the end.
    input_.pop_back();
    while (!input_.empty() && (((unsigned char)input_.back() & 0xC0) == 0x80))
        input_.pop_back();
}

void SearchKeyboard::finish(bool accepted, uint64_t now) {
    if (closing_) return;
    accepted_ = accepted;
    closing_ = true;
    closeAtUs_ = now;
    eraseHeld_ = false;
    touchBackspace_ = -1;
}

void SearchKeyboard::activate(int index, uint64_t now) {
    if (closing_ || index < 0 || index >= (int)keys_.size()) return;
    focused_ = index;
    const Key k = keys_[index];
    switch (k.action) {
    case Action::Character:
        if (input_.size() < kMaxInputBytes) {
            char ch = k.character;
            if (shift_ && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
            input_.push_back(ch);
        }
        if (shift_) { shift_ = false; }
        break;
    case Action::Shift: shift_ = !shift_; break;
    case Action::Numbers: numbers_ = true; more_ = false; rebuild(); break;
    case Action::Letters: numbers_ = false; more_ = false; rebuild(); break;
    case Action::More: more_ = !more_; rebuild(); break;
    case Action::Space: if (input_.size() < kMaxInputBytes) input_.push_back(' '); break;
    case Action::Backspace: eraseOne(); nextEraseUs_ = now + 420000ULL; break;
    case Action::Confirm: finish(true, now); break;
    case Action::Cancel: finish(false, now); break;
    }
}

int SearchKeyboard::hitTest(int x, int y) const {
    for (int i = 0; i < (int)keys_.size(); ++i) {
        const Key& k = keys_[i];
        if (x >= k.x && x < k.x + k.width &&
            y >= k.y && y < k.y + k.height) return i;
    }
    return -1;
}

void SearchKeyboard::update() {
    if (!active_) return;
    const uint64_t now = timeUs();
    if (closing_) {
        if (now - closeAtUs_ >= kAnimationUs) {
            active_ = false;
            ready_ = true;
        }
        return;
    }

    SceCtrlData pad{};
    const int ctrl = sceCtrlPeekBufferPositive(0, &pad, 1);
    const unsigned buttons = ctrl > 0 ? pad.buttons : 0;
    const unsigned pressed = buttons & ~previousButtons_;
    previousButtons_ = buttons;

    SceTouchData touch{};
    const bool down = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) > 0 &&
                      touch.reportNum > 0;
    int touchKey = -1;
    if (down) {
        // Keys are stored at their final positions. Account for panel animation.
        float vis = smooth((float)(now - openedAtUs_) / kAnimationUs);
        const int offset = (int)((1.f - vis) * 510.f);
        touchKey = hitTest(touch.report[0].x * kScreenW / 1920,
                           touch.report[0].y * kScreenH / 1088 - offset);
    }
    if (now - openedAtUs_ < 160000ULL) {
        touchWasDown_ = down;
        return;
    }

    if (down && !touchWasDown_ && touchKey >= 0) {
        touchBackspace_ = (keys_[touchKey].action == Action::Backspace) ? touchKey : -1;
        activate(touchKey, now);
    }
    if (!down) touchBackspace_ = -1;
    touchWasDown_ = down;
    if (closing_) return;

    const unsigned nav = buttons & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_UP | SCE_CTRL_DOWN);
    if (nav != lastNav_) {
        nextNavUs_ = now + 290000ULL;
    }
    if (nav && (nav != lastNav_ || now >= nextNavUs_)) {
        if (nav & SCE_CTRL_LEFT) navigate(-1);
        else if (nav & SCE_CTRL_RIGHT) navigate(1);
        else if (nav & SCE_CTRL_UP) navigate(-2);
        else navigate(2);
        if (nav == lastNav_) nextNavUs_ = now + 115000ULL;
    }
    lastNav_ = nav;
    if (pressed & SCE_CTRL_TRIANGLE) {
        numbers_ = !numbers_;
        more_ = false;
        rebuild();
    }
    if (pressed & SCE_CTRL_LTRIGGER) { shift_ = !shift_; }
    if (pressed & SCE_CTRL_CIRCLE) { finish(false, now); return; }
    if (pressed & SCE_CTRL_START) { finish(true, now); return; }
    if (pressed & SCE_CTRL_CROSS) activate(focused_, now);
    if (closing_) return;
    if (pressed & SCE_CTRL_SQUARE) {
        eraseOne();
        nextEraseUs_ = now + 420000ULL;
    }
    const bool holding = (buttons & SCE_CTRL_SQUARE) ||
        ((buttons & SCE_CTRL_CROSS) && focused_ >= 0 &&
         focused_ < (int)keys_.size() &&
         keys_[focused_].action == Action::Backspace) ||
        (down && touchBackspace_ >= 0 && touchKey == touchBackspace_);
    if (holding && !eraseHeld_) {
        eraseHeld_ = true;
        if (nextEraseUs_ == 0 || nextEraseUs_ < now) nextEraseUs_ = now + 420000ULL;
    }
    if (holding && eraseHeld_ && now >= nextEraseUs_) {
        eraseOne();
        nextEraseUs_ = now + 105000ULL;
    }
    if (!holding) { eraseHeld_ = false; nextEraseUs_ = 0; }
}

bool SearchKeyboard::takeResult(std::string& text, bool& accepted) {
    if (!ready_) return false;
    ready_ = false;
    accepted = accepted_;
    text = input_;
    return true;
}

void SearchKeyboard::drawOverlay(const UiFont* font, const KeyboardPalette& colors) const {
    if (!active_) return;
    const uint64_t now = timeUs();
    const uint64_t elapsed = closing_ ? now - closeAtUs_ : now - openedAtUs_;
    float visibility = smooth((float)elapsed / kAnimationUs);
    if (closing_) visibility = 1.f - visibility;
    const int offset = (int)((1.f - visibility) * 510.f);
    const int panelY = kPanelY + offset;

    // Same catalog frame, no black clear or second swap. Gentle modal dim.
    vita2d_draw_rectangle(0, 0, kScreenW, kScreenH,
                          RGBA8(0,0,0,(unsigned)(145.f * visibility)));
    vita2d_draw_rectangle(kPanelX, panelY, kPanelW, kPanelH, colors.border);
    vita2d_draw_rectangle(kPanelX + 2, panelY + 2, kPanelW - 4,
                          kPanelH - 4, colors.surface);
    vita2d_draw_rectangle(kPanelX + 2, panelY + 2, kPanelW - 4, 5, colors.accent);

    uiDrawText(font, kPanelX + 24, panelY + 32, colors.text, 1.0f, title_.c_str());

    const int inputX = kPanelX + 22;
    const int inputW = kPanelW - 44;
    const int inputY = panelY + 48;
    vita2d_draw_rectangle(inputX, inputY, inputW, 59, colors.border);
    vita2d_draw_rectangle(inputX + 2, inputY + 2, inputW - 4, 55, colors.surface2);
    std::string visible = input_;
    while (!visible.empty() && uiTextWidth(font, .89f, visible.c_str()) > inputW - 50)
        visible.erase(visible.begin());
    if (visible.empty()) visible = ::psvitaalive::L("KEYBOARD_PLACEHOLDER");
    else if (!closing_ && ((now / 500000ULL) & 1) == 0) visible.push_back('|');
    uiDrawText(font, inputX + 15, inputY + 37, colors.text, .89f, visible.c_str());
    const int progressY = panelY + 127;
    const char* navHint = ::psvitaalive::L("KEYBOARD_NAV_HINT");
    float hintScale = .63f;
    while(hintScale > .42f && uiTextWidth(font,hintScale,navHint) > inputW)
        hintScale -= .03f;
    uiDrawText(font, inputX, progressY, colors.dim, hintScale, navHint);

    for (int i = 0; i < (int)keys_.size(); ++i) {
        const Key& k = keys_[i];
        const int y = k.y + offset;
        const bool focused = focused_ == i;
        const bool special = k.action != Action::Character;
        const bool affirmative = k.action == Action::Confirm;
        const unsigned border = focused ? colors.accent : colors.border;
        const unsigned fill = focused ? colors.accent :
            affirmative ? colors.surface2 : (special ? colors.surface2 : colors.surface);
        vita2d_draw_rectangle(k.x, y, k.width, k.height, border);
        vita2d_draw_rectangle(k.x + 2, y + 2, k.width - 4, k.height - 4, fill);
        const unsigned color = focused ? colors.bg :
            affirmative ? colors.accent : colors.text;
        // Draw the case that will actually be inserted by the next keystroke.
        std::string caption = k.label;
        if(k.action == Action::Character && k.character >= 'a' && k.character <= 'z')
            caption.assign(1, shift_ ? (char)(k.character - ('a'-'A')) : k.character);
        else if(k.action == Action::Shift) caption = ::psvitaalive::L("KEYBOARD_SHIFT");
        else if(k.action == Action::Backspace) caption = ::psvitaalive::L("KEYBOARD_DELETE");
        else if(k.action == Action::Cancel) caption = ::psvitaalive::L("KEYBOARD_CANCEL");
        else if(k.action == Action::Confirm) caption = ::psvitaalive::L("KEYBOARD_CONFIRM");
        else if(k.action == Action::Space) caption = ::psvitaalive::L("KEYBOARD_SPACE");
        else if(k.action == Action::More) caption = ::psvitaalive::L("KEYBOARD_MORE");
        float scale = special ? .75f : .89f;
        while(scale > .40f && uiTextWidth(font,scale,caption.c_str()) > k.width-8)
            scale -= .04f;
        centered(font, k.x, y + 37, k.width, color, scale, caption.c_str());
        if (k.action == Action::Shift && shift_)
            vita2d_draw_rectangle(k.x + 9, y + k.height - 7, k.width - 18, 3,
                                  focused ? colors.bg : colors.accent);
    }
    const char* bottomHint = ::psvitaalive::L("KEYBOARD_TOUCH_HINT");
    float bottomScale = .65f;
    while(bottomScale > .40f && uiTextWidth(font,bottomScale,bottomHint) > kPanelW-32)
        bottomScale -= .03f;
    centered(font, kPanelX, panelY + 463, kPanelW, colors.dim, bottomScale,
             bottomHint);
}

} // namespace psvitaalive::ui
