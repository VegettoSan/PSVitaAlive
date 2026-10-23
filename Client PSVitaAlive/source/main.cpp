/* PSVitaAlive - native client entry point. */
#include "localization/localization.hpp"
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/apputil.h>
#include <psp2/message_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <vita2d.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include "diagnostic_logger.hpp"
#include "storage/storage_manager.hpp"
#include "installer/install_controller.hpp"
#include "installer/install_archive_policy.hpp"
#include "ui/ui_font.hpp"
#include "ui/full_catalog_screen.hpp"
#include "ui/image_cache.hpp"
#include "catalog/catalog_manager.hpp"
#include "catalog/catalog_trace.hpp"
#include "update/startup_update_manager.hpp"
#include "network/archive_service_probe.hpp"
#include "update/update_checker.hpp"
#include "installer/install_space_policy.hpp"

namespace {
std::string installStatusText(const psvitaalive::InstallStatus&s){using S=psvitaalive::InstallStatus::State;if(s.state==S::Idle)return{};char b[384];uint64_t p=s.total?std::min<uint64_t>(100,(s.current*100)/s.total):0;sceClibSnprintf(b,sizeof(b),"%s | %s | %llu%% | %s",s.stage.c_str(),s.fileName.empty()?"file":s.fileName.c_str(),(unsigned long long)p,s.message.c_str());return b;}
bool peekFrontTouch(int& outX, int& outY, bool& down) {
    static bool touchInited = false;
    if (!touchInited) {
        sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
        touchInited = true;
    }
    SceTouchData td{};
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1) <= 0) {
        down = false;
        return false;
    }
    if (td.reportNum <= 0) {
        down = false;
        return false;
    }
    outX = td.report[0].x * 960 / 1920;
    outY = td.report[0].y * 544 / 1088;
    down = true;
    return true;
}

// In-app ZIP destination picker (matches PSVitaAlive look).
// Quick paths avoid the system IME; "Escribir ruta..." still uses IME for custom paths.
enum class ZipDestChoice {
    Cancel = 0,
    QuickData,
    QuickApp,
    QuickRepatch,
    QuickPspIso,   // ux0:pspemu/ISO/  (ISO/CSO)
    QuickPspGame,  // ux0:pspemu/PSP/GAME/  (PSP/PS1 PBP)
    CustomIme
};

ZipDestChoice promptZipDestinationChoice() {
    constexpr unsigned SURFACE2 = RGBA8(0x2A, 0x2A, 0x2A, 255);
    constexpr unsigned PANEL = RGBA8(0x20, 0x20, 0x20, 255);
    constexpr unsigned ACCENT = RGBA8(0x3B, 0xFF, 0, 255);
    constexpr unsigned ACCENT_SOFT = RGBA8(0x2A, 0xB0, 0, 255);
    constexpr unsigned TEXT = RGBA8(0xAA, 0xAA, 0xAA, 255);
    constexpr unsigned WHITE = RGBA8(255, 255, 255, 255);
    constexpr unsigned DIM = RGBA8(0x6E, 0x6E, 0x6E, 255);
    constexpr unsigned ROW_BG = RGBA8(0x2E, 0x2E, 0x2E, 255);
    constexpr int SW = 960, SH = 544;

    struct ZipOption {
        const char* path;
        const char* desc;
    };
    static const ZipOption kOptions[] = {
        { "ux0:data/",            "Homebrew app data and game data files" },
        { "ux0:app/",             "PS Vita apps - homebrew or games" },
        { "ux0:repatch/",         "Mods and game patches (rePatch)" },
        { "ux0:pspemu/ISO/",      "PSP / PS1 disc images (ISO or CSO)" },
        { "ux0:pspemu/PSP/GAME/", "PSP / PS1 folder-style games and EBOOTs" },
        { "Custom path...",       "Type any extraction folder manually" },
    };
    constexpr int kCount = 6;

    ::psvitaalive::ui::UiFont font = ::psvitaalive::ui::loadDefaultUiFont();
    if (!font) return ZipDestChoice::Cancel;

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    uint32_t prev = 0;
    {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        prev = pad.buttons;
    }

    int focus = 0;
    ZipDestChoice result = ZipDestChoice::Cancel;
    bool decided = false;
    static bool touchWasDown = false;

    auto choiceFromFocus = [](int f) -> ZipDestChoice {
        switch (f) {
            case 0: return ZipDestChoice::QuickData;
            case 1: return ZipDestChoice::QuickApp;
            case 2: return ZipDestChoice::QuickRepatch;
            case 3: return ZipDestChoice::QuickPspIso;
            case 4: return ZipDestChoice::QuickPspGame;
            default: return ZipDestChoice::CustomIme;
        }
    };

    while (!decided) {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        const uint32_t pressed = pad.buttons & ~prev;
        prev = pad.buttons;

        if (pressed & SCE_CTRL_UP) {
            focus = (focus > 0) ? (focus - 1) : (kCount - 1);
        }
        if (pressed & SCE_CTRL_DOWN) {
            focus = (focus + 1 < kCount) ? (focus + 1) : 0;
        }
        if (pressed & SCE_CTRL_CROSS) {
            result = choiceFromFocus(focus);
            decided = true;
        } else if (pressed & SCE_CTRL_CIRCLE) {
            result = ZipDestChoice::Cancel;
            decided = true;
        }

        const int boxW = 720;
        const int boxH = 500;
        const int boxX = (SW - boxW) / 2;
        const int boxY = (SH - boxH) / 2;
        const int rowH = 56;
        const int listTop = boxY + 92;
        const int listLeft = boxX + 22;
        const int listW = boxW - 44;

        {
            int tx = 0, ty = 0;
            bool touchDown = false;
            peekFrontTouch(tx, ty, touchDown);
            if (touchDown && !touchWasDown) {
                const bool inside =
                    tx >= boxX && tx < boxX + boxW &&
                    ty >= boxY && ty < boxY + boxH;
                if (!inside) {
                    result = ZipDestChoice::Cancel;
                    decided = true;
                } else {
                    for (int i = 0; i < kCount; ++i) {
                        const int ry = listTop + i * rowH;
                        if (ty >= ry && ty < ry + rowH - 4 &&
                            tx >= listLeft && tx < listLeft + listW) {
                            focus = i;
                            result = choiceFromFocus(focus);
                            decided = true;
                            break;
                        }
                    }
                }
            }
            touchWasDown = touchDown;
        }

        vita2d_start_drawing();
        vita2d_set_clear_color(RGBA8(0x12, 0x12, 0x12, 255));
        vita2d_clear_screen();
        vita2d_draw_rectangle(0, 0, SW, SH, RGBA8(0, 0, 0, 120));

        vita2d_draw_rectangle(boxX, boxY, boxW, boxH, PANEL);
        vita2d_draw_rectangle(boxX, boxY, boxW, 3, ACCENT);
        vita2d_draw_rectangle(boxX, boxY + boxH - 1, boxW, 1, ACCENT_SOFT);
        vita2d_draw_rectangle(boxX, boxY, 2, boxH, ACCENT);
        vita2d_draw_rectangle(boxX + boxW - 2, boxY, 2, boxH, ACCENT_SOFT);

        ::psvitaalive::ui::uiDrawText(&font, boxX + 28, boxY + 32, ACCENT, 0.62f, "PSVitaAlive");
        ::psvitaalive::ui::uiDrawText(&font, boxX + 28, boxY + 58, WHITE, 1.00f, "ZIP file detected");
        ::psvitaalive::ui::uiDrawText(&font, boxX + 28, boxY + 80, TEXT, 0.58f,
            "Choose where to extract the archive contents:");

        for (int i = 0; i < kCount; ++i) {
            const int ry = listTop + i * rowH;
            const bool on = (i == focus);
            if (on) {
                vita2d_draw_rectangle(listLeft, ry, listW, rowH - 6, ROW_BG);
                vita2d_draw_rectangle(listLeft, ry, 4, rowH - 6, ACCENT);
            } else {
                vita2d_draw_rectangle(listLeft, ry, listW, rowH - 6, SURFACE2);
            }
            const unsigned pathCol = on ? ACCENT : WHITE;
            const unsigned descCol = on ? TEXT : DIM;
            ::psvitaalive::ui::uiDrawText(&font, listLeft + 16, ry + 18, pathCol, 0.72f, kOptions[i].path);
            ::psvitaalive::ui::uiDrawText(&font, listLeft + 16, ry + 38, descCol, 0.52f, kOptions[i].desc);
        }

        vita2d_draw_rectangle(boxX + 1, boxY + boxH - 36, boxW - 2, 35, SURFACE2);
        ::psvitaalive::ui::uiDrawText(&font, boxX + 28, boxY + boxH - 14, DIM, 0.52f,
            "D-Pad / Touch: select    Cross: confirm    Circle: cancel");

        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceKernelDelayThread(16 * 1000);
    }

    touchWasDown = false;
    vita2d_wait_rendering_done();
    font.reset();
    return result;
}

bool promptZipDestination(std::string& dst, psvitaalive::ui::FullCatalogScreen& screen) {
    const ZipDestChoice choice = promptZipDestinationChoice();
    if (choice == ZipDestChoice::Cancel) {
        psvitaalive::diagnostics::log("[UI] ZIP destination cancelled at path picker");
        return false;
    }
    if (choice == ZipDestChoice::QuickData) {
        dst = "ux0:data/";
        return true;
    }
    if (choice == ZipDestChoice::QuickApp) {
        dst = "ux0:app/";
        return true;
    }
    if (choice == ZipDestChoice::QuickRepatch) {
        dst = "ux0:repatch/";
        return true;
    }
    if (choice == ZipDestChoice::QuickPspIso) {
        dst = "ux0:pspemu/ISO/";
        return true;
    }
    if (choice == ZipDestChoice::QuickPspGame) {
        dst = "ux0:pspemu/PSP/GAME/";
        return true;
    }

    // Custom path uses the same embedded keyboard as catalog search.
    std::string custom = dst.empty() ? "ux0:data/" : dst;
    if (!screen.promptTextBlocking(custom, ::psvitaalive::L("KEYBOARD_ZIP_TITLE"), custom)) {
        return false;
    }
    for (char& c : custom) {
        if (c == '\\') c = '/';
    }
    while (custom.size() > 1 && custom.back() == '/') {
        custom.pop_back();
    }
    if (custom.empty()) return false;
    dst = custom;
    return true;
}


uint64_t parseCatalogSizeBytes(const std::string& raw) {
    if (raw.empty()) return 0;
    std::string s;
    s.reserve(raw.size());
    for (unsigned char c : raw) {
        if (c == ' ' || c == '\t') continue;
        if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
        s.push_back(static_cast<char>(c));
    }
    if (s.empty()) return 0;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return 0;
    std::string u = end ? std::string(end) : std::string();
    uint64_t mul = 1;
    if (u.empty() || u == "b") {
        if (s.find('.') == std::string::npos) {
            uint64_t n = 0;
            for (char c : s) {
                if (c < '0' || c > '9') break;
                n = n * 10ULL + static_cast<uint64_t>(c - '0');
            }
            if (n > 0) return n;
        }
        mul = 1;
    } else if (u == "k" || u == "kb" || u == "kib") mul = 1024ULL;
    else if (u == "m" || u == "mb" || u == "mib") mul = 1024ULL * 1024ULL;
    else if (u == "g" || u == "gb" || u == "gib") mul = 1024ULL * 1024ULL * 1024ULL;
    else if (u == "t" || u == "tb" || u == "tib") mul = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
    else return 0;
    if (v <= 0.0) return 0;
    return static_cast<uint64_t>(v * static_cast<double>(mul) + 0.5);
}

int hexValue(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
std::string urlDecodePath(std::string value){for(int pass=0;pass<2;++pass){std::string out;out.reserve(value.size());bool changed=false;for(size_t i=0;i<value.size();++i){if(value[i]=='%'&&i+2<value.size()){const int h=hexValue(value[i+1]);const int l=hexValue(value[i+2]);if(h>=0&&l>=0){out.push_back(static_cast<char>((h<<4)|l));i+=2;changed=true;continue;}}out.push_back(value[i]);}value.swap(out);if(!changed)break;}return value;}
std::string fileNameFromUrl(const std::string&url,const std::string&id){
    std::string clean=url;const size_t q=clean.find('?');if(q!=std::string::npos)clean.erase(q);const size_t f=clean.find('#');if(f!=std::string::npos)clean.erase(f);
    std::string name;const size_t media=clean.find("mediafire.com");const size_t marker=clean.find("/file/",media==std::string::npos?0:media);
    if(media!=std::string::npos&&marker!=std::string::npos){const size_t idEnd=clean.find('/',marker+6);if(idEnd!=std::string::npos){const size_t nameEnd=clean.find('/',idEnd+1);name=clean.substr(idEnd+1,nameEnd==std::string::npos?std::string::npos:nameEnd-(idEnd+1));}}
    if(name.empty()){const size_t slash=clean.find_last_of('/');name=slash==std::string::npos?clean:clean.substr(slash+1);if(name=="file"&&slash!=std::string::npos){const size_t prev=clean.find_last_of('/',slash-1);if(prev!=std::string::npos)name=clean.substr(prev+1,slash-prev-1);}}
    name=urlDecodePath(name);return name.empty()||name=="file"?id+".bin":name;
}
bool isZipName(const std::string&name){if(name.size()<4)return false;std::string ext=name.substr(name.size()-4);for(char&c:ext){if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');}return ext==".zip";}
std::string progressMessage(uint64_t current,uint64_t total,const std::string&prefix,const std::string&file){char b[320];const uint64_t pct=total?std::min<uint64_t>(100,(current*100)/total):0;sceClibSnprintf(b,sizeof(b),"%s | %llu%% | %llu / %llu%s%s",prefix.c_str(),(unsigned long long)pct,(unsigned long long)current,(unsigned long long)total,file.empty()?"":" | ",file.c_str());return b;}
}

int main(){
    psvitaalive::diagnostics::init();
    psvitaalive::diagnostics::log("============================================================");
    psvitaalive::diagnostics::log("PSVitaAlive session BEGIN");
    PSVA_TRACE_EVENT("build_marker","event-driven-ux0-space-20261012 main=untouched fix=1");
    const uint64_t catTraceStarted=psvitaalive::catalog_trace::nowUs();
    psvitaalive::diagnostics::log("TitleID=PSVAS1178");

    // AppUtil stays initialized for other console services, never for a Sony keyboard.
    {
        SceAppUtilInitParam params{};
        SceAppUtilBootParam boot{};
        if(sceAppUtilInit(&params, &boot) < 0)
            psvitaalive::diagnostics::log("[System] AppUtil initialization failed");
    }

    psvitaalive::StorageManager storage;storage.initProjectDirs();
    psvitaalive::InstallController installer;psvitaalive::CatalogManager catalogs;psvitaalive::ui::ImageCache images;
    if(!installer.init())psvitaalive::diagnostics::log("[System] InstallController init failed");
    if (!psvitaalive::LocalizationManager::instance().initialize(installer.settings())) {
        psvitaalive::diagnostics::log("[System] LocalizationManager initialize failed — English fallback");
    } else {
        psvitaalive::diagnostics::log(std::string("[System] UI language=") +
            psvitaalive::LocalizationManager::instance().currentLanguageName());
    }

    psvitaalive::ui::FullCatalogScreen screen;screen.setImageCache(&images);
    screen.setCatalogChangeCallback([&](psvitaalive::ui::CatalogType next){psvitaalive::diagnostics::log(std::string("[UI] catalog requested: ")+psvitaalive::ui::catalogName(next));images.cancelQueuedRequests();return catalogs.request(next);});
    screen.setSearchCallback([&screen](const std::string& current){
        screen.openSearchKeyboard(current);
    });
    screen.setInstallCancelCallback([&installer](){ installer.cancel(); });
    screen.setInstallAcknowledgeCallback([&installer](){ installer.acknowledgeResult(); });
    screen.setInstallZipRecoveryCallback([&installer, &screen](bool keep){ const bool resolved=installer.resolveZipRecovery(keep); if(resolved) screen.requestUx0SpaceRefresh(); return resolved; });
    screen.setSavedZipCallbacks(
        [&installer](const psvitaalive::ui::CatalogItem& item) {
            std::vector<psvitaalive::ui::FullCatalogScreen::SavedZipOption> options;
            for (const auto& archive : installer.savedZips(item.id, item.titleId))
                options.push_back({archive.fileName, archive.archivePath});
            return options;
        },
        [&installer, &screen](const psvitaalive::ui::CatalogItem& item,
                             const std::string& archivePath) {
            const bool started = installer.requestSavedZipExtraction(
                item.id, item.titleId, archivePath);
            if (started) screen.requestUx0SpaceRefresh();
            return started;
        });
    screen.setInstallCallbacks([&installer, &screen](const psvitaalive::ui::CatalogItem&item){psvitaalive::diagnostics::log("[UI] INSTALL REQUEST name="+item.name+" title_id="+item.titleId+" url="+item.downloadUrl);
    if(item.downloadUrl.empty()||item.downloadFileName.empty())return false;
        std::string zrif;std::string ltype;std::string cid;std::string extractPath;
        for(const auto&L:item.linkDetails){if(L.url==item.downloadUrl){zrif=L.zrif;ltype=L.type;cid=L.contentId;extractPath=L.extractPath;break;}}
        std::string zipDestination;
        if(isZipName(item.downloadFileName) || psvitaalive::isDataOrGameFilesLinkType(ltype)){
            if(!extractPath.empty()){
                zipDestination=extractPath;
                psvitaalive::diagnostics::log(std::string("[UI] ZIP extract_path from catalog: ")+zipDestination);
            }else{
                zipDestination="ux0:data/";
                if(!promptZipDestination(zipDestination, screen)){psvitaalive::diagnostics::log("[UI] ZIP destination cancelled");return false;}
            }
        }
        {
            uint64_t exp = parseCatalogSizeBytes(item.size);
            for (const auto& link : item.linkDetails) {
                if (link.url == item.downloadUrl) {
                    exp = psvitaalive::chooseSelectedLinkSizeBytes(
                        parseCatalogSizeBytes(link.size), exp,
                        link.url, item.downloadUrl);
                    break;
                }
            }
            installer.setPendingCatalogMeta(item.id, item.version, item.versionDate, 0, item.titleId);
            screen.prepareForHeavyWork();
            return installer.requestInstall(item.downloadUrl,item.downloadFileName,zipDestination,zrif,ltype,cid,item.name,exp);
        }},[&installer](){return installStatusText(installer.status());});
    screen.setLinkActionCallback([&installer, &screen](const psvitaalive::ui::CatalogItem&item,const psvitaalive::ui::CatalogLink&link){
        const std::string type=link.type;
        const bool isPlugin=(type=="Plugin"||type=="plugin"||type=="Plugins"||type=="plugins");
        const bool actionable=isPlugin||(type=="Download"||type=="download"||type=="Downloads"||type=="Mirror"||type=="mirror"||type=="DLC"||type=="dlc"||type=="Data Files"||type=="Game Files"||link.url.find(".vpk")!=std::string::npos||link.url.find(".pkg")!=std::string::npos||link.url.find(".zip")!=std::string::npos||link.url.find(".pbp")!=std::string::npos||link.url.find(".iso")!=std::string::npos||link.url.find(".cso")!=std::string::npos||link.url.find(".suprx")!=std::string::npos||link.url.find(".skprx")!=std::string::npos);
        if(!actionable){psvitaalive::diagnostics::log(std::string("[UI] link is informational only: ")+link.url);return false;}
        psvitaalive::ui::CatalogItem requestItem=item;requestItem.downloadUrl=link.url;requestItem.downloadFileName=fileNameFromUrl(link.url,item.id);
        // Plugin: keep a stable on-device name (e.g. kubridge.skprx) even if the remote
        // URL uses a versioned filename like "kubridge v0.3.1 hotfix.skprx".
        if(isPlugin){
            auto baseFrom = [](const std::string& s)->std::string{
                if(s.empty()) return {};
                std::string u=s;
                const size_t q=u.find('?'); if(q!=std::string::npos) u.resize(q);
                const size_t slash=u.find_last_of("/\\");
                std::string n=(slash==std::string::npos)?u:u.substr(slash+1);
                // decode minimal %20 for readability in job paths only when still needed
                return n;
            };
            if(!link.line.empty()){
                const std::string n=baseFrom(link.line);
                if(!n.empty()) requestItem.downloadFileName=n;
            }else if(!link.name.empty() && (link.name.find(".skprx")!=std::string::npos || link.name.find(".suprx")!=std::string::npos || link.name.find(".skprx")!=std::string::npos)){
                requestItem.downloadFileName=link.name;
            }
        }
        psvitaalive::diagnostics::log(std::string("[UI] LINK INSTALL name=")+item.name+" type="+link.type+" url="+link.url);
        std::string zipDestination;
        if(isPlugin){
            zipDestination = link.extractPath.empty() ? "ur0:tai/" : link.extractPath;
            psvitaalive::diagnostics::log(std::string("[UI] Plugin extract_path: ")+zipDestination+" section="+link.section+" line="+link.line);
        } else if(isZipName(requestItem.downloadFileName) || psvitaalive::isDataOrGameFilesLinkType(link.type)){
            // Prefer per-link extract_path when present; otherwise ask the user.
            if(!link.extractPath.empty()){
                zipDestination=link.extractPath;
                psvitaalive::diagnostics::log(std::string("[UI] ZIP extract_path from catalog: ")+zipDestination);
            }else{
                zipDestination="ux0:data/";
                if(!promptZipDestination(zipDestination, screen)){
                    psvitaalive::diagnostics::log("[UI] ZIP destination cancelled");
                    return false;
                }
            }
        }
        {
            const uint64_t exp = psvitaalive::chooseSelectedLinkSizeBytes(
                parseCatalogSizeBytes(link.size), parseCatalogSizeBytes(item.size),
                link.url, item.downloadUrl);
            installer.setPendingCatalogMeta(item.id, item.version, item.versionDate, 0, item.titleId);
            screen.prepareForHeavyWork();
            return installer.requestInstall(requestItem.downloadUrl,requestItem.downloadFileName,zipDestination,link.zrif,link.type,link.contentId,item.name,exp,link.section,link.line);
        }
    });

    if(!screen.init()){psvitaalive::diagnostics::log("[System] UI initialization failed");installer.shutdown();catalogs.shutdown();images.shutdown();psvitaalive::diagnostics::shutdown();sceKernelExitProcess(1);return 1;}

    // VitaShell pattern: remove temporary self-update helper bubble if present.
    psvitaalive::UpdateChecker::cleanupUpdaterBubble();

    if (installer.settings().startupUpdateCheck) {
        psvitaalive::diagnostics::log("[Startup] update check enabled by config");
        psvitaalive::StartupUpdateManager startupUpdate;
        screen.setCatalogLoading(true, ::psvitaalive::L(::psvitaalive::TextId::StartupUpdateLabel), 0, 0, ::psvitaalive::L(::psvitaalive::TextId::CheckingForAppUpdates));
        if(!startupUpdate.start(PSVITAALIVE_VERSION)){
            screen.showToast(::psvitaalive::L(::psvitaalive::TextId::UpdateStartupUnavailable), 1800);
        }else{
            while(startupUpdate.isBusy()){
                const auto us = startupUpdate.snapshot();
                screen.setCatalogLoading(true, ::psvitaalive::L(::psvitaalive::TextId::StartupUpdateLabel), us.current, us.total,
                                          us.message.empty()?::psvitaalive::L(::psvitaalive::TextId::UpdatingApplicationMsg):us.message);
                if(!screen.updateAndDraw()){
                    startupUpdate.requestCancel();
                    break;
                }
                sceKernelDelayThread(16 * 1000);
            }
            startupUpdate.wait();
            const auto us = startupUpdate.snapshot();
            if(us.restartRequired){
                // Launch PSVAUPDT1 exactly like updater launches PSVAS1178:
                // LaunchAppByUri + ExitProcess (see updater/main.c launchClientAndExit).
                // Avoid DestroyOtherApp and heavy teardown that can hang client→updater.
                screen.setCatalogLoading(true, ::psvitaalive::L(::psvitaalive::TextId::SelfUpdateLabel), 1, 1, ::psvitaalive::L(::psvitaalive::TextId::StartingUpdaterMsg));
                screen.updateAndDraw();
                psvitaalive::diagnostics::log("[Startup] main-thread handoff to PSVAUPDT1 (updater-style)");
                // Stop background workers so Shell is not fighting live threads.
                catalogs.shutdown();
                images.shutdown();
                installer.shutdown();
                screen.shutdown();
                psvitaalive::diagnostics::log("PSVitaAlive session END — launching updater");
                psvitaalive::diagnostics::shutdown();
                psvitaalive::UpdateChecker::launchUpdaterAndExit(); // LaunchAppByUri + ExitProcess
            }
            if(us.state == psvitaalive::StartupUpdateManager::State::Failed)
                screen.showToast(::psvitaalive::L(::psvitaalive::TextId::UpdateUnavailableContinue), 1800);
            else if(us.state == psvitaalive::StartupUpdateManager::State::Cancelled)
                screen.showToast("Update cancelled — continuing", 1600);
        }
    } else {
        psvitaalive::diagnostics::log("[Startup] update check disabled by config");
    }

    psvitaalive::diagnostics::log("[Startup] update phase finished — starting catalogs");


#ifndef PSVITAALIVE_STARTUP_HOMEBREW_ONLY
#define PSVITAALIVE_STARTUP_HOMEBREW_ONLY 1
#endif

    // Catalog and image workers do not exist until the startup update phase is finished.
    {
        const uint64_t t=psvitaalive::catalog_trace::nowUs();
        const bool ok=catalogs.init();
        PSVA_TRACE_DURATION("catalog_manager_init",t,std::string("ok=")+(ok?"1":"0"));
        if(!ok)psvitaalive::diagnostics::log("[System] CatalogManager init failed");
    }
    auto imageCacheStartupProgress=[&screen](psvitaalive::ui::ImageCache::StartupMaintenancePhase phase,uint64_t current,uint64_t total){
        const char*message=::psvitaalive::L("IMAGE_CACHE_CHECKING");
        if(phase==psvitaalive::ui::ImageCache::StartupMaintenancePhase::Cleaning)message=::psvitaalive::L("IMAGE_CACHE_CLEANING");
        else if(phase==psvitaalive::ui::ImageCache::StartupMaintenancePhase::Ready)message=::psvitaalive::L("IMAGE_CACHE_READY");
        screen.setCatalogLoading(true,::psvitaalive::L("IMAGE_CACHE_LABEL"),current,total,message);
        screen.updateAndDraw();
    };
    {
        const uint64_t t=psvitaalive::catalog_trace::nowUs();
        const bool ok=images.init(imageCacheStartupProgress);
        PSVA_TRACE_DURATION("image_cache_startup_init",t,std::string("ok=")+(ok?"1":"0"));
        if(!ok)psvitaalive::diagnostics::log("[System] ImageCache init failed");
    }
    screen.setImageCache(&images);

    const int catalogCount=(int)psvitaalive::ui::CatalogType::Count;
    int preloadIndex=0;bool startupCatalogs=true;bool homebrewReady=false;bool startupThemePending=false;bool startupNewsPending=false;bool startupEssentialPending=false;

    screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);
    screen.setCatalogLoading(true,psvitaalive::ui::catalogName(psvitaalive::ui::CatalogType::Homebrew),0,0,::psvitaalive::L(::psvitaalive::TextId::LoadingHomebrewCatalogMsg));
    PSVA_TRACE_EVENT("startup_homebrew_request");
    if(!catalogs.request(psvitaalive::ui::CatalogType::Homebrew)){startupCatalogs=false;screen.setCatalogError(::psvitaalive::L(::psvitaalive::TextId::UnableToStartCatalogCheck));}

        
    screen.setAppSettings(installer.settings());
    screen.setPluginStatus(installer.plugins());
    screen.setSettingsSaveCallback([&installer, &screen](const psvitaalive::AppSettingsData& s) {
        installer.setSettings(s);
        screen.setAppSettings(s);
        psvitaalive::LocalizationManager::instance().setMode(s.languageMode, s.language);
    });

    // Defer plugin warning toasts until the UI has drawn at least once.
    // Showing toasts before the main loop has crashed on some Vita3K setups when
    // NoPspEmuDrm is missing (detection itself is fine — see PluginDetector logs).
    bool pluginWarnPending =
        installer.settings().startupPluginDetection && installer.settings().warnMissingPlugins;
    int pluginWarnFrames = 0;
    psvitaalive::diagnostics::log("[Startup] entering main loop (pluginWarnPending=" +
        std::string(pluginWarnPending ? "1" : "0") + ")");

    // Refresh UX0 only when a USER download or install finishes a phase.
    // ImageCache thumbnails/captures are deliberately excluded.
    using StorageInstallState = psvitaalive::InstallStatus::State;
    StorageInstallState previousStorageInstallState = installer.status().state;
    psvitaalive::ArchiveServiceProbe archiveProbe;
    bool archiveProbeStarted = false;
    bool archiveProbeHandled = false;
while(true){
        if(!screen.updateAndDraw()) break;        if(!screen.updateAndDraw()) break;

        // After a few frames, surface plugin warnings safely.        // After a few frames, surface plugin warnings safely.
        if (pluginWarnPending) {
            ++pluginWarnFrames;
            if (pluginWarnFrames >= 3) {
                pluginWarnPending = false;
                const auto& pl = installer.plugins();
                psvitaalive::diagnostics::log(
                    std::string("[Startup] plugin warn toast nonpdrm=") +
                    (pl.nonpdrm ? "1" : "0") +
                    " nopspK=" + (pl.nopspemudrmKern ? "1" : "0") +
                    " nopspU=" + (pl.nopspemudrmUser ? "1" : "0"));
                // NoPspEmuDrm missing: no startup toast (still detected in Settings + install path).
                if (!pl.nonpdrm) {
                    screen.showToast("NoNpDrm not found: licensed Vita PKGs may fail", 4500);
                }
            }
        }
        const uint64_t now=sceKernelGetSystemTimeWide();
        psvitaalive::CatalogManager::Status cs=catalogs.status();
        {
            static uint64_t lastTraceUs=0;
            const uint64_t traceNow=psvitaalive::catalog_trace::nowUs();
            if(traceNow-catTraceStarted<20000000ULL &&
               (lastTraceUs==0 || traceNow-lastTraceUs>=300000ULL)){
                lastTraceUs=traceNow;
                PSVA_TRACE_EVENT("startup_poll",
                    std::string("startup=")+(startupCatalogs?"1":"0")+
                    " state="+std::to_string((int)cs.state)+
                    " soft="+std::to_string(cs.softRefresh?1:0)+
                    " homebrew_ready="+std::to_string(homebrewReady?1:0)+
                    " progress="+std::to_string(cs.current)+"/"+std::to_string(cs.total));
            }
        }
        if(startupCatalogs&&cs.state==psvitaalive::CatalogManager::State::Loading){screen.setCatalogLoading(true,cs.label,cs.current,cs.total,progressMessage(cs.current,cs.total,cs.message,""));}
        else if(startupCatalogs&&cs.state==psvitaalive::CatalogManager::State::Failed){
            psvitaalive::diagnostics::log(std::string("[Startup] catalog failed: ")+cs.label+" error="+cs.error);
#if PSVITAALIVE_STARTUP_HOMEBREW_ONLY
            startupCatalogs=false;
            startupThemePending=true;
            // Preserve the existing image cache; images load while browsing.
            screen.setCatalogLoading(false,"",0,0,
                homebrewReady?(::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg)):(::psvitaalive::L(::psvitaalive::TextId::CatalogUnavailableMsg)));
            if(!homebrewReady)
                screen.setCatalogError(cs.error.empty()?::psvitaalive::L(::psvitaalive::TextId::UnableToLoadCatalog):cs.error);
            psvitaalive::diagnostics::log("[Startup] homebrew-only startup finished (failed path)");
#else
            ++preloadIndex;
            if(preloadIndex<catalogCount){const auto next=(psvitaalive::ui::CatalogType)preloadIndex;screen.setCatalogLoading(true,psvitaalive::ui::catalogName(next),0,0,::psvitaalive::L(::psvitaalive::TextId::CheckingCatalogOfflineOk));catalogs.request(next);}
            else{
                startupCatalogs=false;startupThemePending=true;screen.setCatalogLoading(false,"",0,0,::psvitaalive::L(::psvitaalive::TextId::CatalogsReadyMsg));psvitaalive::diagnostics::log("[Startup] all catalogs processed; images load on demand");
            }
#endif
        }

        std::vector<psvitaalive::ui::CatalogItem> ready;psvitaalive::ui::CatalogType readyCatalog;
        if(catalogs.takeReady(ready,readyCatalog)){
            psvitaalive::diagnostics::log(std::string("[System] catalog ready: ")+psvitaalive::ui::catalogName(readyCatalog));
            PSVA_TRACE_EVENT("main_received_ready",
                std::string("catalog=")+psvitaalive::ui::catalogName(readyCatalog)+
                " items="+std::to_string(ready.size())+
                " startup="+std::to_string(startupCatalogs?1:0));
            const size_t readyCount=ready.size();
            if(startupCatalogs){
                if(readyCatalog==psvitaalive::ui::CatalogType::Homebrew){
                    const uint64_t uiBegan=psvitaalive::catalog_trace::nowUs();
                    screen.setCatalogItems(std::move(ready));
                    screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);
                    PSVA_TRACE_DURATION("startup_screen_first_set",uiBegan,
                        "items="+std::to_string(readyCount));
                    homebrewReady=true;
                }
#if PSVITAALIVE_STARTUP_HOMEBREW_ONLY
                if(readyCatalog==psvitaalive::ui::CatalogType::Homebrew){
                    startupCatalogs=false;
                    startupThemePending=true;
                    // Catalog images are requested on demand by the UI.
                    screen.setCatalogLoading(false,"",0,0,::psvitaalive::L(::psvitaalive::TextId::CatalogReadyMsg));
                    psvitaalive::diagnostics::log("[Startup] homebrew ready (no multi-catalog preload)");
                }
#else
                ++preloadIndex;
                if(preloadIndex<catalogCount){const auto next=(psvitaalive::ui::CatalogType)preloadIndex;screen.setCatalogLoading(true,psvitaalive::ui::catalogName(next),0,0,::psvitaalive::L(::psvitaalive::TextId::CheckingCatalogOfflineOk));catalogs.request(next);}
                else{startupCatalogs=false;startupThemePending=true;screen.setCatalogLoading(false,"",0,0,::psvitaalive::L(::psvitaalive::TextId::CatalogsReadyMsg));psvitaalive::diagnostics::log("[Startup] all catalogs ready; images load on demand");}
#endif
            }else{
                // Runtime tab load or soft-refresh hot-swap.
                const auto readyStatus=catalogs.status();
                if(readyStatus.message=="Catalog updated" && readyCatalog==screen.activeCatalog()){
                    screen.refreshCatalogItems(std::move(ready));
                }else{
                    screen.setCatalogItems(std::move(ready));
                    screen.setActiveCatalog(readyCatalog);
                }
                if(catalogs.isBusy()){
                    screen.setCatalogLoading(true,psvitaalive::ui::catalogName(readyCatalog),0,0,"Loading next catalog — please wait...");
                }else{
                    screen.setCatalogLoading(false,psvitaalive::ui::catalogName(readyCatalog),1,1,"Ready");
                    // The shared catalog sync indicator owns the update toast.
                }
            }
        }

        if(!startupCatalogs){
            const auto csBusy=catalogs.status();
            if(catalogs.isBusy()||csBusy.state==psvitaalive::CatalogManager::State::Loading){
                const char* fallback =
                    csBusy.softRefresh
                        ? "Updating catalog in background (cache already shown)..."
                        : "Loading this catalog — please wait (not preloaded at startup)...";
                screen.setCatalogLoading(true,
                    csBusy.label.empty()?psvitaalive::ui::catalogName(csBusy.catalog):csBusy.label,
                    csBusy.current,csBusy.total,
                    csBusy.message.empty()?fallback:csBusy.message);
            } else {
                screen.setCatalogLoading(false, "", 0, 0, "");
            }
        }

        // Only actual transfers trigger the thin divider and the three sync toasts.
        // The existing cache-first path, tabs and install/download UI remain untouched.
        {
            using Phase = psvitaalive::CatalogManager::RefreshPhase;
            static unsigned seenSerial = 0;
            static Phase lastToastPhase = Phase::None;
            static uint64_t completedAtMs = 0;
            const auto sync = catalogs.status();
            const uint64_t nowMs = sceKernelGetProcessTimeWide() / 1000ULL;
            if (sync.refreshSerial != seenSerial) {
                seenSerial = sync.refreshSerial;
                lastToastPhase = Phase::None;
                completedAtMs = 0;
            }
            const bool visibleCatalog = !startupCatalogs &&
                sync.catalog == screen.activeCatalog();
            if (visibleCatalog && sync.refreshPhase != lastToastPhase) {
                if (sync.refreshPhase == Phase::Downloading)
                    screen.showToast(::psvitaalive::L("CATALOG_SYNC_DOWNLOADING"), 2400);
                else if (sync.refreshPhase == Phase::Applying)
                    screen.showToast(::psvitaalive::L("CATALOG_SYNC_APPLYING"), 2400);
                else if (sync.refreshPhase == Phase::Updated)
                    screen.showToast(::psvitaalive::L("CATALOG_SYNC_UPDATED"), 2400);
                lastToastPhase = sync.refreshPhase;
            }
            if (sync.refreshPhase == Phase::Updated && completedAtMs == 0)
                completedAtMs = nowMs;
            if (sync.refreshPhase != Phase::Updated) completedAtMs = 0;

            if (!visibleCatalog) {
                screen.setCatalogSyncProgress(false, 0.f);
            } else if (sync.refreshPhase == Phase::Downloading) {
                const float progress = sync.total > 0
                    ? 0.9f * std::min(1.0, (double)sync.current / (double)sync.total)
                    : 0.f;
                screen.setCatalogSyncProgress(true, progress, sync.total == 0);
            } else if (sync.refreshPhase == Phase::Applying) {
                // Parsing has no byte-level progress callback: hold at downloaded 90%.
                screen.setCatalogSyncProgress(true, 0.9f);
            } else {
                const bool complete = sync.refreshPhase == Phase::Updated &&
                    nowMs - completedAtMs < 750ULL;
                screen.setCatalogSyncProgress(complete, complete ? 1.f : 0.f);
            }
        }

const psvitaalive::InstallStatus cur=installer.status();using InstallState=psvitaalive::InstallStatus::State;
        // One refresh on Downloading -> Installing (download finished), and
        // one on any terminal install outcome (success/error/cancel/cleanup).
        // State-edge detection prevents refreshing on every UI frame.
        if (cur.state != previousStorageInstallState) {
            if (cur.state == StorageInstallState::Failed &&
                cur.archiveSourceUnavailable) {
                // Defer the notice until the user acknowledges the existing
                // install error dialog; never obscure failure details/recovery.
                screen.queueArchiveServiceWarning(true);
                psvitaalive::diagnostics::log("[ArchiveProbe] user download failed on Archive.org; notice queued");
            }
            const bool downloaded = previousStorageInstallState == InstallState::Downloading
                                    && cur.state == InstallState::Installing;
            const bool finished = cur.state == InstallState::Completed
                               || cur.state == InstallState::Failed
                               || cur.state == InstallState::Cancelled;
            if (downloaded || finished) screen.requestUx0SpaceRefresh();
            previousStorageInstallState = cur.state;
        }
        images.setNetworkPaused(screen.keyboardVisible()||cur.state==InstallState::Downloading||cur.state==InstallState::Installing);const bool active=cur.state==InstallState::Downloading||cur.state==InstallState::Installing||cur.state==InstallState::Completed||cur.state==InstallState::Failed||cur.state==InstallState::Cancelled;int outcome=0;if(cur.state==InstallState::Completed)outcome=1;else if(cur.state==InstallState::Cancelled)outcome=3;else if(cur.state==InstallState::Failed)outcome=2;screen.setInstallProgress(active,cur.current,cur.total,cur.bytesPerSecond,cur.stage,cur.fileName,cur.message,outcome,cur.liveAreaOk,cur.installPath,cur.titleId,cur.resultAutoCloseRemainingMs,cur.needsReboot,cur.zipRecoveryAvailable,cur.zipRecoveryPath,cur.zipRecoveryExtractPath,cur.zipRecoveryMayBeCorrupt,cur.archiveSourceUnavailable);
        if(startupThemePending && !active && !screen.isThemeSetupVisible()){
            screen.openThemeSetupIfNeeded();
            if(!screen.isThemeSetupVisible()){
                // Already done or just closed without showing
                startupThemePending=false;
                startupNewsPending=true;
            }
        }
        // While theme modal is open, wait (do not open News yet).
        if(startupThemePending && screen.isThemeSetupVisible()){
            // stay pending until user saves
        } else if(startupThemePending && !screen.isThemeSetupVisible()){
            startupThemePending=false;
            startupNewsPending=true;
        }
        if(startupNewsPending && !active && !screen.isNewsVisible() && !screen.isThemeSetupVisible()){
            screen.runNewsCheck(false);
            if(screen.isNewsCheckDone() || screen.isNewsVisible()){
                startupNewsPending=false;
                startupEssentialPending=true;
            }
        }
        // After theme + news: recommend kubridge / fd_fix / libshacccg if missing
        if(startupEssentialPending && !active
           && !screen.isNewsVisible() && !screen.isThemeSetupVisible()
           && !screen.isEssentialPluginsVisible() && !screen.isEssentialPluginsInstalling()
           && !screen.isPluginRebootModal()){
            if(!screen.essentialPluginsPromptDone()){
                screen.tryShowEssentialPluginsPrompt();
            }
            if(screen.essentialPluginsPromptDone()
               && !screen.isEssentialPluginsVisible()
               && !screen.isEssentialPluginsInstalling()){
                startupEssentialPending=false;
            }
        }

        // Only start the one-shot server probe after THE ENTIRE startup modal
        // sequence (theme, news, recommended plugins) has finished.
        // It runs in a bounded background thread; never in render/input work.
        const bool startupDialogsFinished =
            !startupCatalogs && !startupThemePending && !startupNewsPending &&
            !startupEssentialPending &&
            !screen.isThemeSetupVisible() && !screen.isNewsVisible() &&
            !screen.isEssentialPluginsVisible() && !screen.isEssentialPluginsInstalling() &&
            !screen.isPluginRebootModal();
        if (startupDialogsFinished && !archiveProbeStarted) {
            archiveProbeStarted = true;
            if (archiveProbe.start())
                psvitaalive::diagnostics::log("[ArchiveProbe] startup check launched");
            else
                psvitaalive::diagnostics::log("[ArchiveProbe] startup check unavailable");
        }
        if (archiveProbeStarted && !archiveProbeHandled &&
            archiveProbe.result() != psvitaalive::ArchiveServiceProbe::Result::Pending) {
            archiveProbeHandled = true;
            if (archiveProbe.result() == psvitaalive::ArchiveServiceProbe::Result::Unreachable)
                screen.queueArchiveServiceWarning(false);
        }
        if (startupDialogsFinished && !active)
            screen.openArchiveServiceWarningIfReady();
    }

    screen.setInstallProgress(false,0,0,0,"","","",0,false,"","");screen.shutdown();installer.shutdown();catalogs.shutdown();images.shutdown();sceAppUtilShutdown();psvitaalive::diagnostics::log("PSVitaAlive session END");psvitaalive::diagnostics::shutdown();sceKernelExitProcess(0);return 0;
}
