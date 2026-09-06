#pragma once

#include "installer/app_settings.hpp"

#include <string>
#include <vector>
#include <vita2d.h>

namespace psvitaalive {
namespace ui {

const char* uiFontStyleKey(UiFontStyle style);

/** Scan app0:font/ and ux0:data/psvitaalive/fonts/ for *.pgf basenames. */
std::vector<std::string> listAvailableUiFonts();

/**
 * Load UI PGF.
 * @param style  Legacy preferred file (serif.pgf / sans.pgf / …) when customFile empty
 * @param customFile  Basename of any .pgf you placed in the font folders
 */
vita2d_pgf* loadUiFont(UiFontStyle style, const std::string& customFile);
vita2d_pgf* loadUiFont(UiFontStyle style); // customFile empty

} // namespace ui
} // namespace psvitaalive
