#include "installer/app_settings.hpp"
#include "storage/storage_manager.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>

#include <cstring>
#include <string>

namespace psvitaalive {
namespace {

constexpr const char* kConfigPath = "ux0:data/psvitaalive/config.json";

bool containsKey(const std::string& json, const char* key, std::string& valueOut) {
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
        default: return "auto";
    }
}

const char* AppSettings::toString(PspTarget t) {
    switch (t) {
        case PspTarget::LiveArea: return "livearea";
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

const char* AppSettings::toString(PspMediaFormat f) {
    switch (f) {
        case PspMediaFormat::Iso: return "iso";
        default: return "folder";
    }
}

PspMediaFormat AppSettings::parsePspMediaFormat(const std::string& s) {
    if (s == "iso") return PspMediaFormat::Iso;
    return PspMediaFormat::Folder;
}

const char* AppSettings::toString(ColorTheme t) {
    switch (t) {
        case ColorTheme::Cyan: return "cyan";
        case ColorTheme::Rose: return "rose";
        case ColorTheme::Amber: return "amber";
        case ColorTheme::Violet: return "violet";
        case ColorTheme::Mono: return "mono";
        case ColorTheme::Oled: return "oled";
        case ColorTheme::PsVita: return "psvita";
        case ColorTheme::Crimson: return "crimson";
        case ColorTheme::Coffee: return "coffee";
        case ColorTheme::Gold: return "gold";
        case ColorTheme::Emerald: return "emerald";
        case ColorTheme::Coral: return "coral";
        case ColorTheme::Teal: return "teal";
        case ColorTheme::Indigo: return "indigo";
        case ColorTheme::Sky: return "sky";
        case ColorTheme::Magenta: return "magenta";
        case ColorTheme::Mint: return "mint";
        case ColorTheme::Sunset: return "sunset";
        case ColorTheme::Ocean: return "ocean";
        case ColorTheme::Lavender: return "lavender";
        case ColorTheme::Cherry: return "cherry";
        case ColorTheme::Sand: return "sand";
        case ColorTheme::Forest: return "forest";
        case ColorTheme::Ice: return "ice";
        case ColorTheme::Grape: return "grape";
        case ColorTheme::Peach: return "peach";
        case ColorTheme::Azure: return "azure";
        case ColorTheme::Steel: return "steel";
        case ColorTheme::Honey: return "honey";
        case ColorTheme::Midnight: return "midnight";
        case ColorTheme::Sakura: return "sakura";
        case ColorTheme::Matrix: return "matrix";
        case ColorTheme::Scarlet: return "scarlet";
        case ColorTheme::Orange: return "orange";
        case ColorTheme::White: return "white";
        case ColorTheme::Snow: return "snow";
        case ColorTheme::Ivory: return "ivory";
        case ColorTheme::Khaki: return "khaki";
        case ColorTheme::Terracotta: return "terracotta";
        case ColorTheme::Ruby: return "ruby";
        case ColorTheme::Copper: return "copper";
        case ColorTheme::Olive: return "olive";
        case ColorTheme::Maroon: return "maroon";
        case ColorTheme::Turquoise: return "turquoise";
        case ColorTheme::Lemon: return "lemon";
        case ColorTheme::Plum: return "plum";
        case ColorTheme::Navy: return "navy";
        case ColorTheme::Rust: return "rust";
        case ColorTheme::Champagne: return "champagne";
        case ColorTheme::Graphite: return "graphite";
        case ColorTheme::NeonLime:
        default: return "neon";
    }
}

ColorTheme AppSettings::parseColorTheme(const std::string& s) {
    if (s == "cyan" || s == "blue") return ColorTheme::Cyan;
    if (s == "rose" || s == "pink" || s == "rosal") return ColorTheme::Rose;
    if (s == "amber") return ColorTheme::Amber;
    if (s == "violet" || s == "purple") return ColorTheme::Violet;
    if (s == "mono" || s == "gray" || s == "grey") return ColorTheme::Mono;
    if (s == "oled" || s == "black") return ColorTheme::Oled;
    if (s == "psvita" || s == "vita" || s == "playstation") return ColorTheme::PsVita;
    if (s == "crimson") return ColorTheme::Crimson;
    if (s == "coffee" || s == "brown" || s == "cafe") return ColorTheme::Coffee;
    if (s == "gold") return ColorTheme::Gold;
    if (s == "emerald") return ColorTheme::Emerald;
    if (s == "coral") return ColorTheme::Coral;
    if (s == "teal") return ColorTheme::Teal;
    if (s == "indigo") return ColorTheme::Indigo;
    if (s == "sky") return ColorTheme::Sky;
    if (s == "magenta" || s == "fuchsia") return ColorTheme::Magenta;
    if (s == "mint") return ColorTheme::Mint;
    if (s == "sunset") return ColorTheme::Sunset;
    if (s == "ocean") return ColorTheme::Ocean;
    if (s == "lavender") return ColorTheme::Lavender;
    if (s == "cherry") return ColorTheme::Cherry;
    if (s == "sand" || s == "beige") return ColorTheme::Sand;
    if (s == "forest") return ColorTheme::Forest;
    if (s == "ice") return ColorTheme::Ice;
    if (s == "grape") return ColorTheme::Grape;
    if (s == "peach") return ColorTheme::Peach;
    if (s == "azure") return ColorTheme::Azure;
    if (s == "steel") return ColorTheme::Steel;
    if (s == "honey") return ColorTheme::Honey;
    if (s == "midnight") return ColorTheme::Midnight;
    if (s == "sakura") return ColorTheme::Sakura;
    if (s == "matrix") return ColorTheme::Matrix;
    if (s == "scarlet" || s == "red") return ColorTheme::Scarlet;
    if (s == "orange") return ColorTheme::Orange;
    if (s == "white") return ColorTheme::White;
    if (s == "snow") return ColorTheme::Snow;
    if (s == "ivory") return ColorTheme::Ivory;
    if (s == "khaki") return ColorTheme::Khaki;
    if (s == "terracotta") return ColorTheme::Terracotta;
    if (s == "ruby") return ColorTheme::Ruby;
    if (s == "copper") return ColorTheme::Copper;
    if (s == "olive") return ColorTheme::Olive;
    if (s == "maroon") return ColorTheme::Maroon;
    if (s == "turquoise") return ColorTheme::Turquoise;
    if (s == "lemon" || s == "yellow") return ColorTheme::Lemon;
    if (s == "plum") return ColorTheme::Plum;
    if (s == "navy") return ColorTheme::Navy;
    if (s == "rust") return ColorTheme::Rust;
    if (s == "champagne") return ColorTheme::Champagne;
    if (s == "graphite") return ColorTheme::Graphite;
    return ColorTheme::NeonLime;
}

AppSettingsData AppSettings::load() {
    AppSettingsData data;
    SceUID fd = sceIoOpen(kConfigPath, SCE_O_RDONLY, 0);
    if (fd < 0) return data;
    char buf[1024];
    const int n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    if (n <= 0) return data;
    buf[n] = '\0';
    const std::string json(buf);

    std::string v;
    if (containsKey(json, "install_method", v)) data.installMethod = parseInstallMethod(v);
    if (containsKey(json, "psp_target", v)) data.pspTarget = parsePspTarget(v);
    if (containsKey(json, "psp_media_format", v)) data.pspMediaFormat = parsePspMediaFormat(v);
    if (containsKey(json, "color_theme", v)) data.colorTheme = parseColorTheme(v);
    bool b = true;
    if (containsBool(json, "warn_missing_plugins", b)) data.warnMissingPlugins = b;
    if (containsBool(json, "prompt_image_warmup", b)) data.promptImageWarmup = b;
    const bool hasThemeSetupDone = containsBool(json, "theme_setup_done", b);
    if (hasThemeSetupDone) data.themeSetupDone = b;
    else data.themeSetupDone = true;
    const bool hasStartupPluginDetection = containsBool(json, "startup_plugin_detection", b);
    if (hasStartupPluginDetection) data.startupPluginDetection = b;
    const bool hasStartupUpdateCheck = containsBool(json, "startup_update_check", b);
    if (hasStartupUpdateCheck) data.startupUpdateCheck = b;

    if (!hasStartupPluginDetection || !hasStartupUpdateCheck || !hasThemeSetupDone)
        save(data);

    sceClibPrintf("[AppSettings] loaded theme=%s\n", toString(data.colorTheme));
    return data;
}

bool AppSettings::save(const AppSettingsData& data) {
    StorageManager st;
    st.createDirectories(StorageManager::BASE_DIR);
    char json[896];
    sceClibSnprintf(
        json, sizeof(json),
        "{\n"
        "  \"install_method\": \"%s\",\n"
        "  \"psp_target\": \"%s\",\n"
        "  \"psp_media_format\": \"%s\",\n"
        "  \"color_theme\": \"%s\",\n"
        "  \"warn_missing_plugins\": %s,\n"
        "  \"prompt_image_warmup\": %s,\n"
        "  \"theme_setup_done\": %s,\n"
        "  \"startup_plugin_detection\": %s,\n"
        "  \"startup_update_check\": %s\n"
        "}\n",
        toString(data.installMethod),
        toString(data.pspTarget),
        toString(data.pspMediaFormat),
        toString(data.colorTheme),
        data.warnMissingPlugins ? "true" : "false",
        data.promptImageWarmup ? "true" : "false",
        data.themeSetupDone ? "true" : "false",
        data.startupPluginDetection ? "true" : "false",
        data.startupUpdateCheck ? "true" : "false");
    SceUID fd = sceIoOpen(kConfigPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return false;
    const int wr = sceIoWrite(fd, json, std::strlen(json));
    sceIoClose(fd);
    return wr > 0;
}

} // namespace psvitaalive
