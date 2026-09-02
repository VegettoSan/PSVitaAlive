#pragma once

#include <string>

namespace psvitaalive {

enum class InstallMethod {
    Auto = 0,   // direct for now; BGDL when available
    Direct,
    Bgdl
};

enum class PspTarget {
    Adrenaline = 0, // ux0:pspemu/ISO or GAME
    LiveArea        // requires NoPspEmuDrm (same paths; bubble depends on plugin)
};

/** UI accent / surface palette (Settings -> Color theme). Default keeps current Neon Lime. */
enum class ColorTheme {
    NeonLime = 0, // brand default #3BFF00
    Cyan,         // blue / cyan
    Rose,         // pink / rose
    Amber,        // warm orange
    Violet,       // purple
    Mono,         // silver / grayscale
    Oled,         // pure black + soft mint
    PsVita,       // classic PlayStation Vita UI blue
    Crimson,      // strong red
    Coffee,       // warm brown
    Gold,         // rich gold
    Emerald,      // deep emerald green
    Coral,        // soft coral
    Teal,         // calm teal
    Indigo,       // deep indigo
    Count
};

struct AppSettingsData {
    InstallMethod installMethod = InstallMethod::Auto;
    PspTarget pspTarget = PspTarget::Adrenaline;
    ColorTheme colorTheme = ColorTheme::NeonLime;
    bool warnMissingPlugins = true;
    /** If false, skip the startup "download all images?" dialog (on-demand only). */
    bool promptImageWarmup = false;
    // Internal startup diagnostics; intentionally hidden from the UI.
    bool startupPluginDetection = true;
    bool startupUpdateCheck = true;
};

/**
 * Persist user installer preferences under ux0:data/psvitaalive/config.json
 */
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
