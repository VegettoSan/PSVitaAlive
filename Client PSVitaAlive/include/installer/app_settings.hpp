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

/** UI accent / surface palette (Settings -> Color theme). */
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
    // Expanded variety
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
    Count
};

struct AppSettingsData {
    InstallMethod installMethod = InstallMethod::Auto;
    PspTarget pspTarget = PspTarget::Adrenaline;
    ColorTheme colorTheme = ColorTheme::NeonLime;
    bool warnMissingPlugins = true;
    bool promptImageWarmup = false;
    bool themeSetupDone = false;
    bool startupPluginDetection = true;
    bool startupUpdateCheck = true;
};

class AppSettings {
public:
    static AppSettingsData load();
    static bool save(const AppSettingsData& data);

    static const char* toString(InstallMethod m);
    static const char* toString(PspTarget t);
    static const char* toString(ColorTheme t);

    static InstallMethod parseInstallMethod(const std::string& s);
    static PspTarget parsePspTarget(const std::string& s);
    static ColorTheme parseColorTheme(const std::string& s);
};

} // namespace psvitaalive
