#pragma once

#include <string>

namespace psvitaalive {

enum class InstallMethod {
    Auto = 0,
    Direct,
    Bgdl
};

enum class PspTarget {
    Adrenaline = 0,
    LiveArea
};

/** Adrenaline PSP PKG layout (PKGj-style). Folder = EBOOT.PBP under GAME; Iso = converted ISO. */
enum class PspMediaFormat {
    Folder = 0,  // default — pspemu/PSP/GAME/<ID>/EBOOT.PBP
    Iso          // pspemu/ISO/*.iso
};

/** UI accent / surface palette. Each value must stay visually distinct. */
enum class ColorTheme {
    NeonLime = 0,
    Cyan,
    Rose,
    Amber,
    Violet,
    Mono,
    Oled,
    PsVita,
    Crimson,
    Coffee,
    Gold,
    Emerald,
    Coral,
    Teal,
    Indigo,
    Sky,
    Magenta,
    Mint,
    Sunset,
    Ocean,
    Lavender,
    Cherry,
    Sand,
    Forest,
    Ice,
    Grape,
    Peach,
    Azure,
    Steel,
    Honey,
    Midnight,
    Sakura,
    Matrix,
    Scarlet,
    Orange,
    White,
    Snow,
    Ivory,
    Khaki,
    Terracotta,
    Ruby,
    Copper,
    Olive,
    Maroon,
    Turquoise,
    Lemon,
    Plum,
    Navy,
    Rust,
    Champagne,
    Graphite,
    Count
};

enum class LanguageMode {
    System = 0,
    Manual
};

/** Legacy fixed slots (still used if uiFontFile is empty and matching .pgf exists). */
enum class UiFontStyle {
    Default = 0,   // vita2d_load_default_pgf
    Serif,         // serif.pgf
    Sans,          // sans.pgf
    SerifBold,     // serif_bold.pgf
    SansBold,      // sans_bold.pgf
    Count
};

struct AppSettingsData {
    InstallMethod installMethod = InstallMethod::Auto;
    PspTarget pspTarget = PspTarget::Adrenaline;
    PspMediaFormat pspMediaFormat = PspMediaFormat::Folder;
    ColorTheme colorTheme = ColorTheme::NeonLime;
    bool warnMissingPlugins = true;
    bool promptImageWarmup = false;
    bool themeSetupDone = false;
    bool startupPluginDetection = true;
    bool startupUpdateCheck = true;
    LanguageMode languageMode = LanguageMode::System;
    std::string language = "en";
    UiFontStyle uiFontStyle = UiFontStyle::Default;
};

class AppSettings {
public:
    static AppSettingsData load();
    static bool save(const AppSettingsData& data);

    static const char* toString(InstallMethod m);
    static const char* toString(PspTarget t);
    static const char* toString(PspMediaFormat f);
    static const char* toString(ColorTheme t);
    static const char* toString(LanguageMode m);
    static const char* toString(UiFontStyle f);

    static InstallMethod parseInstallMethod(const std::string& s);
    static PspTarget parsePspTarget(const std::string& s);
    static PspMediaFormat parsePspMediaFormat(const std::string& s);
    static ColorTheme parseColorTheme(const std::string& s);
    static LanguageMode parseLanguageMode(const std::string& s);
    static UiFontStyle parseUiFontStyle(const std::string& s);
};

} // namespace psvitaalive
