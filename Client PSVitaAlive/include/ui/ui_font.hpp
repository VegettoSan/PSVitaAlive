#pragma once

#include "installer/app_settings.hpp"

#include <string>
#include <utility>
#include <vector>
#include <vita2d.h>

namespace psvitaalive {
namespace ui {

/**
 * Thin font handle: either system/custom PGF or FreeType TTF/OTF.
 * Drawing uses the same float "scale" as the old PGF path; FreeType maps
 * scale → pixel size so existing call sites stay readable.
 */
struct UiFont {
    enum class Kind { None, Pgf, FreeType };

    Kind kind = Kind::None;
    vita2d_pgf*  pgf = nullptr;
    vita2d_font* ft  = nullptr;

    UiFont() = default;
    UiFont(const UiFont&) = delete;
    UiFont& operator=(const UiFont&) = delete;
    UiFont(UiFont&& o) noexcept { *this = std::move(o); }
    UiFont& operator=(UiFont&& o) noexcept {
        if (this == &o) return *this;
        reset();
        kind = o.kind; pgf = o.pgf; ft = o.ft;
        o.kind = Kind::None; o.pgf = nullptr; o.ft = nullptr;
        return *this;
    }
    ~UiFont() { reset(); }

    explicit operator bool() const { return kind != Kind::None; }
    void reset();
};

const char* uiFontStyleKey(UiFontStyle style);

/** Scan app0:font/ and ux0:data/psvitaalive/fonts/ for .pgf / .ttf / .otf */
std::vector<std::string> listAvailableUiFonts();

/** Load preferred style file, explicit basename, or default PGF. */
UiFont loadUiFont(UiFontStyle style, const std::string& customFile);
UiFont loadUiFont(UiFontStyle style);

/** Default system PGF (startup / one-off dialogs). */
UiFont loadDefaultUiFont();

/** User size multiplier (50–150%). Applied on top of each draw scale. */
void setUiFontScalePercent(int percent);
int  getUiFontScalePercent();

/** Draw / measure — scale matches existing PGF call sites (e.g. 0.74f). */
void uiDrawText(const UiFont* font, int x, int y, unsigned color, float scale, const char* text);
int  uiTextWidth(const UiFont* font, float scale, const char* text);

} // namespace ui
} // namespace psvitaalive
