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

struct AppSettingsData {
    InstallMethod installMethod = InstallMethod::Auto;
    PspTarget pspTarget = PspTarget::Adrenaline;
    bool warnMissingPlugins = true;
};

/**
 * Persist user installer preferences under ux0:data/psvitaalive/config.json
 */
class AppSettings {
public:
    static constexpr const char* kConfigPath = "ux0:data/psvitaalive/config.json";

    static AppSettingsData load();
    static bool save(const AppSettingsData& data);

    static const char* toString(InstallMethod m);
    static const char* toString(PspTarget t);
    static InstallMethod parseInstallMethod(const std::string& s);
    static PspTarget parsePspTarget(const std::string& s);
};

} // namespace psvitaalive
