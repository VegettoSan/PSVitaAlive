#pragma once

#include "installer/app_settings.hpp"

#include <vita2d.h>

namespace psvitaalive {
namespace ui {

/** Human-readable name for Settings (English key resolved via L() in UI). */
const char* uiFontStyleKey(UiFontStyle style);

/**
 * Load a vita2d PGF for the given style.
 *
 * Search order:
 *   1. ux0:data/psvitaalive/fonts/<style>.pgf   (user override, no rebuild)
 *   2. app0:font/<style>.pgf                    (bundled in VPK)
 *   3. vita2d_load_default_pgf()
 *
 * Style file names:
 *   serif.pgf | sans.pgf | serif_bold.pgf | sans_bold.pgf
 *
 * Caller owns the pointer (vita2d_free_pgf).
 */
vita2d_pgf* loadUiFont(UiFontStyle style);

} // namespace ui
} // namespace psvitaalive
