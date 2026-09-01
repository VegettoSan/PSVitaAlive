#include "installer/app_settings.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>

#include <cstring>
#include <string>

namespace psvitaalive {
namespace {

bool containsKey(const std::string& json, const char* key, std::string& valueOut) {
    // Minimal JSON string value extractor: "key" : "value"
    const std::string pattern = std::string("\"") + key + "\"";
    size_t p = json.find(pattern);
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    p = json.find('"', p);
    if (p == std::string::npos) return false;
    const size_t start = p + 1;
    const size_t end = json.find('"', start);
    if (end == std::string::npos) return false;
    valueOut = json.substr(start, end - start);
    return true;
}

bool containsBool(const std::string& json, const char* key, bool& out) {
    const std::string pattern = std::string("\"") + key + "\"";
    size_t p = json.find(pattern);
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    while (p < json.size() && (json[p] == ':' || json[p] == ' ' || json[p] == '\t')) ++p;
    if (json.compare(p, 4, "true") == 0) { out = true; return true; }
    if (json.compare(p, 5, "false") == 0) { out = false; return true; }
    return false;
}

} // namespace

const char* AppSettings::toString(InstallMethod m) {
    switch (m) {
        case InstallMethod::Direct: return "direct";
        case InstallMethod::Bgdl: return "bgdl";
        case InstallMethod::Auto:
        default: return "auto";
    }
}

const char* AppSettings::toString(PspTarget t) {
    switch (t) {
        case PspTarget::LiveArea: return "livearea";
        case PspTarget::Adrenaline:
        default: return "adrenaline";
    }
}

InstallMethod AppSettings::parseInstallMethod(const std::string& s) {
    if (s == "direct") return InstallMethod::Direct;
    if (s == "bgdl") return InstallMethod::Bgdl;
    return InstallMethod::Auto;
}

PspTarget AppSettings::parsePspTarget(const std::string& s) {
    if (s == "livearea") return PspTarget::LiveArea;
    return PspTarget::Adrenaline;
}

const char* AppSettings::toString(ColorTheme t) {
    switch (t) {
        case ColorTheme::Cyan: return "cyan";
        case ColorTheme::Rose: return "rose";
        case ColorTheme::Amber: return "amber";
        case ColorTheme::Violet: return "violet";
        case ColorTheme::Mono: return "mono";
        case ColorTheme::Oled: return "oled";
        case ColorTheme::NeonLime:
        default: return "neon";
    }
}

ColorTheme AppSettings::parseColorTheme(const std::string& s) {
    if (s == "cyan" || s == "blue") return ColorTheme::Cyan;
    if (s == "rose" || s == "pink" || s == "rosal") return ColorTheme::Rose;
    if (s == "amber" || s == "orange") return ColorTheme::Amber;
    if (s == "violet" || s == "purple") return ColorTheme::Violet;
    if (s == "mono" || s == "gray" || s == "grey") return ColorTheme::Mono;
    if (s == "oled" || s == "black") return ColorTheme::Oled;
    return ColorTheme::NeonLime;
}

AppSettingsData AppSettings::load() {
    AppSettingsData data;
    StorageManager st;
    st.createDirectories(StorageManager::BASE_DIR);

    SceUID fd = sceIoOpen(kConfigPath, SCE_O_RDONLY, 0);
    if (fd < 0) {
        // Write defaults on first run.
        save(data);
        return data;
    }
    char buf[1024];
    const int n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n <= 0) return data;
    buf[n] = 0;
    const std::string json(buf);

    std::string v;
    if (containsKey(json, "install_method", v)) data.installMethod = parseInstallMethod(v);
    if (containsKey(json, "psp_target", v)) data.pspTarget = parsePspTarget(v);
    if (containsKey(json, "color_theme", v)) data.colorTheme = parseColorTheme(v);
    bool b = true;
    const bool hasWarnMissingPlugins = containsBool(json, "warn_missing_plugins", b);
    if (hasWarnMissingPlugins) data.warnMissingPlugins = b;
    const bool hasPromptImageWarmup = containsBool(json, "prompt_image_warmup", b);
    if (hasPromptImageWarmup) data.promptImageWarmup = b;
    const bool hasStartupPluginDetection = containsBool(json, "startup_plugin_detection", b);
    if (hasStartupPluginDetection) data.startupPluginDetection = b;
    const bool hasStartupUpdateCheck = containsBool(json, "startup_update_check", b);
    if (hasStartupUpdateCheck) data.startupUpdateCheck = b;

    // Older config.json files do not contain the internal startup switches.
    // Persist the default=true values once so the user can toggle them manually.
    if (!hasStartupPluginDetection || !hasStartupUpdateCheck) {
        save(data);
    }

    sceClibPrintf("[AppSettings] loaded method=%s psp=%s theme=%s warn=%d imagesPrompt=%d pluginDetect=%d updateCheck=%d\n",
                  toString(data.installMethod), toString(data.pspTarget), toString(data.colorTheme),
                  data.warnMissingPlugins ? 1 : 0, data.promptImageWarmup ? 1 : 0,
                  data.startupPluginDetection ? 1 : 0, data.startupUpdateCheck ? 1 : 0);
    return data;
}

bool AppSettings::save(const AppSettingsData& data) {
    StorageManager st;
    st.createDirectories(StorageManager::BASE_DIR);
    char json[640];
    sceClibSnprintf(
        json, sizeof(json),
        "{\n"
        "  \"install_method\": \"%s\",\n"
        "  \"psp_target\": \"%s\",\n"
        "  \"color_theme\": \"%s\",\n"
        "  \"warn_missing_plugins\": %s,\n"
        "  \"prompt_image_warmup\": %s,\n"
        "  \"startup_plugin_detection\": %s,\n"
        "  \"startup_update_check\": %s\n"
        "}\n",
        toString(data.installMethod),
        toString(data.pspTarget),
        toString(data.colorTheme),
        data.warnMissingPlugins ? "true" : "false",
        data.promptImageWarmup ? "true" : "false",
        data.startupPluginDetection ? "true" : "false",
        data.startupUpdateCheck ? "true" : "false"
    );
    SceUID fd = sceIoOpen(kConfigPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return false;
    const int wr = sceIoWrite(fd, json, std::strlen(json));
    sceIoClose(fd);
    return wr > 0;
}

} // namespace psvitaalive
