#pragma once

#include "installer/app_settings.hpp"

#include <vita2d.h>

namespace psvitaalive {
namespace ui {

/** Human-readable name for Settings (English key resolved via L() in UI). */
const char* uiFontStyleKey(UiFontStyle style);

/**
 * Load a vita2d PGF for the given style.
 * Uses console system fonts under sa0:data/font/ when available.
 * Always falls back to vita2d_load_default_pgf() if a custom file is missing.
 * Caller owns the pointer (vita2d_free_pgf).
 */
vita2d_pgf* loadUiFont(UiFontStyle style);

} // namespace ui
} // namespace psvitaalive
