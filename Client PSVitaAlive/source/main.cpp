/* PSVitaAlive - native client entry point. */
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/ime_dialog.h>
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/message_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <vita2d.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <string>
#include <unordered_set>
#include <vector>
#include "diagnostic_logger.hpp"
#include "storage/storage_manager.hpp"
#include "installer/install_controller.hpp"
#include "ui/full_catalog_screen.hpp"
#include "ui/image_cache.hpp"
#include "catalog/catalog_manager.hpp"
#include "update/startup_update_manager.hpp"
#include "update/update_checker.hpp"

namespace {
std::string installStatusText(const psvitaalive::InstallStatus&s){using S=psvitaalive::InstallStatus::State;if(s.state==S::Idle)return{};char b[384];uint64_t p=s.total?std::min<uint64_t>(100,(s.current*100)/s.total):0;sceClibSnprintf(b,sizeof(b),"%s | %s | %llu%% | %s",s.stage.c_str(),s.fileName.empty()?"file":s.fileName.c_str(),(unsigned long long)p,s.message.c_str());return b;}
bool asciiToWide(const std::string&text,SceWChar16*out,size_t cap){if(!out||!cap)return false;size_t i=0;for(;i+1<cap&&i<text.size();++i){unsigned char c=(unsigned char)text[i];out[i]=(SceWChar16)(c<128?c:'?');}out[i]=0;return true;}
std::string wideToAscii(const SceWChar16*t){if(!t)return{};std::string r;for(size_t i=0;t[i]&&i<2048;++i)r.push_back(t[i]<=0x7F?(char)t[i]:'?');return r;}
bool promptText(const std::string& initial, const std::string& title, std::string& out) {
    // Vita-safe IME (aligned with VitaSDK sample + VitaShell):
    //   - sceAppUtilInit + sceCommonDialogSetConfigParam at startup
    //   - separate UTF-16 buffers for initialText vs inputTextBuffer (same buffer crashes)
    //   - no re-entry; term leftover dialogs before init
    //   - render loop with vita2d_common_dialog_update()
    static bool imeModuleLoaded = false;
    static bool imeBusy = false;
    if (imeBusy) {
        sceClibPrintf("[UI] IME re-entry blocked\n");
        return false;
    }

    if (!imeModuleLoaded) {
        const int r = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
        if (r < 0 && r != static_cast<int>(0x8002D013) /* already loaded variants vary */) {
            // Some firmwares return success or "already loaded"; only hard-fail on real errors
            // if status stays unusable after init.
            sceClibPrintf("[UI] SCE_SYSMODULE_IME load: 0x%08X (continuing)\n", r);
        }
        imeModuleLoaded = true;
    }

    // IMPORTANT: initialText and inputTextBuffer MUST be distinct (VitaShell / SDK sample).
    constexpr SceUInt32 kMaxLen = 128; // enough for search; avoids huge stack + dialog stress
    SceWChar16 inputBuf[kMaxLen + 1];
    SceWChar16 initialBuf[kMaxLen + 1];
    SceWChar16 titleBuf[SCE_IME_DIALOG_MAX_TITLE_LENGTH + 1];
    sceClibMemset(inputBuf, 0, sizeof(inputBuf));
    sceClibMemset(initialBuf, 0, sizeof(initialBuf));
    sceClibMemset(titleBuf, 0, sizeof(titleBuf));

    asciiToWide(initial, initialBuf, kMaxLen + 1);
    asciiToWide(initial, inputBuf, kMaxLen + 1);
    asciiToWide(title.empty() ? "Search" : title, titleBuf, SCE_IME_DIALOG_MAX_TITLE_LENGTH + 1);

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    // Broad language mask like VitaShell; still forced so dialog opens consistently.
    param.supportedLanguages = 0x0001FFFF;
    param.languagesForced = SCE_TRUE;
    param.type = SCE_IME_TYPE_BASIC_LATIN;
    param.option = SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
    param.title = titleBuf;
    param.maxTextLength = kMaxLen;
    param.initialText = initialBuf;
    param.inputTextBuffer = inputBuf;
    param.enterLabel = SCE_IME_ENTER_LABEL_SEARCH;

    imeBusy = true;
    const int initRes = sceImeDialogInit(&param);
    if (initRes < 0) {
        sceClibPrintf("[UI] sceImeDialogInit failed: 0x%08X\n", initRes);
        // Best-effort cleanup if half-open
        if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_NONE) {
            sceImeDialogTerm();
        }
        imeBusy = false;
        return false;
    }

    // Cap wait so a stuck dialog cannot hang the process forever.
    int spin = 0;
    bool aborted = false;
    SceCommonDialogStatus status = SCE_COMMON_DIALOG_STATUS_NONE;
    while ((status = sceImeDialogGetStatus()) != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_draw_rectangle(0, 0, 960, 544, RGBA8(0, 0, 0, 180));
        vita2d_end_drawing();
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
        sceKernelDelayThread(16 * 1000); // ~60 FPS pacing
        if (++spin > 60 * 120) {
            sceClibPrintf("[UI] IME wait timeout — aborting dialog\n");
            sceImeDialogAbort();
            aborted = true;
            break;
        }
    }

    // Abort is asynchronous: let the common dialog reach FINISHED before reading
    // the result or terminating it. This avoids tearing down an active IME state.
    if (aborted) {
        for (int cleanupSpin = 0; cleanupSpin < 120; ++cleanupSpin) {
            status = sceImeDialogGetStatus();
            if (status == SCE_COMMON_DIALOG_STATUS_FINISHED) break;
            vita2d_common_dialog_update();
            sceKernelDelayThread(16 * 1000);
        }
    }

    SceImeDialogResult result{};
    const int gr = sceImeDialogGetResult(&result);
    if (gr < 0) {
        sceClibPrintf("[UI] sceImeDialogGetResult failed: 0x%08X\n", gr);
    }
    const bool ok = (gr >= 0 && result.button == SCE_IME_DIALOG_BUTTON_ENTER);
    if (ok) {
        out = wideToAscii(inputBuf);
    }

    sceImeDialogTerm();
    imeBusy = false;
    return ok;
}

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

    vita2d_pgf* font = vita2d_load_default_pgf();
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

        vita2d_pgf_draw_text(font, boxX + 28, boxY + 32, ACCENT, 0.62f, "PSVitaAlive");
        vita2d_pgf_draw_text(font, boxX + 28, boxY + 58, WHITE, 1.00f, "ZIP file detected");
        vita2d_pgf_draw_text(font, boxX + 28, boxY + 80, TEXT, 0.58f,
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
            vita2d_pgf_draw_text(font, listLeft + 16, ry + 18, pathCol, 0.72f, kOptions[i].path);
            vita2d_pgf_draw_text(font, listLeft + 16, ry + 38, descCol, 0.52f, kOptions[i].desc);
        }

        vita2d_draw_rectangle(boxX + 1, boxY + boxH - 36, boxW - 2, 35, SURFACE2);
        vita2d_pgf_draw_text(font, boxX + 28, boxY + boxH - 14, DIM, 0.52f,
            "D-Pad / Touch: select    Cross: confirm    Circle: cancel");

        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceKernelDelayThread(16 * 1000);
    }

    touchWasDown = false;
    vita2d_wait_rendering_done();
    vita2d_free_pgf(font);
    return result;
}

bool promptZipDestination(std::string& dst) {
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

    // Custom path via system IME (pre-filled with ux0:data/).
    // Uses the same Vita-safe promptText() as catalog search (AppUtil + dialog update loop).
    std::string custom = dst.empty() ? "ux0:data/" : dst;
    if (!promptText(custom, "ZIP extract path", custom)) {
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


std::string formatEta(uint64_t seconds){if(seconds==0)return "--";uint64_t h=seconds/3600,m=(seconds%3600)/60,sec=seconds%60;char o[64];if(h)sceClibSnprintf(o,sizeof(o),"%llu:%02llu:%02llu",(unsigned long long)h,(unsigned long long)m,(unsigned long long)sec);else sceClibSnprintf(o,sizeof(o),"%02llu:%02llu",(unsigned long long)m,(unsigned long long)sec);return o;}
bool promptDownloadAllImages(size_t totalImages){
    vita2d_wait_rendering_done();
    vita2d_pgf* font=vita2d_load_default_pgf();
    if(!font) return false;
    bool yes=false, done=false;
    int selected=0;
    uint32_t prev=0;
    bool touchWasDown=false;
    {
        SceCtrlData pad{};
        sceCtrlPeekBufferPositive(0,&pad,1);
        prev=pad.buttons;
    }
    while(!done){
        SceCtrlData pad={};
        sceCtrlPeekBufferPositive(0,&pad,1);
        uint32_t pressed=pad.buttons&~prev;
        prev=pad.buttons;
        if(pressed&SCE_CTRL_LEFT) selected=0;
        if(pressed&SCE_CTRL_RIGHT) selected=1;
        if(pressed&SCE_CTRL_CROSS){ yes=selected==0; done=true; }
        if(pressed&SCE_CTRL_CIRCLE){ yes=false; done=true; }

        const unsigned SURFACE=RGBA8(0x37,0x37,0x37,255),BORDER=RGBA8(0x6E,0x6E,0x6E,255);
        const unsigned TEXT=RGBA8(0xAA,0xAA,0xAA,255),DIM=RGBA8(0x6E,0x6E,0x6E,255);
        const unsigned ACCENT=RGBA8(0x3B,0xFF,0,255),WHITE=RGBA8(255,255,255,255);
        const unsigned BLACK=RGBA8(0,0,0,255),PANEL=RGBA8(0x20,0x20,0x20,255);
        const int w=620,h=360,x=(960-w)/2,y=(544-h)/2;
        const int by=y+266,bw=220,bh=38;
        const int btn0x=x+58, btn1x=x+342;

        int tx=0,ty=0; bool touchDown=false;
        peekFrontTouch(tx,ty,touchDown);
        if(touchDown && !touchWasDown){
            if(tx>=btn0x && tx<btn0x+bw && ty>=by && ty<by+bh){ selected=0; yes=true; done=true; }
            else if(tx>=btn1x && tx<btn1x+bw && ty>=by && ty<by+bh){ selected=1; yes=false; done=true; }
        }
        touchWasDown=touchDown;

        vita2d_start_drawing();
        vita2d_draw_rectangle(0,0,960,544,RGBA8(0x18,0x18,0x18,255));
        vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,70));
        vita2d_draw_rectangle(x,y,w,h,PANEL);
        vita2d_draw_rectangle(x,y,w,2,ACCENT);
        vita2d_draw_rectangle(x,y+2,2,h-4,ACCENT);
        vita2d_draw_rectangle(x+w-2,y+2,2,h-4,BORDER);
        vita2d_draw_rectangle(x,y+h-2,w,2,BORDER);
        vita2d_pgf_draw_text(font,x+28,y+36,ACCENT,.68f,"PSVitaAlive");
        vita2d_pgf_draw_text(font,x+28,y+78,WHITE,1.02f,"Download catalog images?");
        vita2d_pgf_draw_text(font,x+28,y+108,TEXT,.58f,"Images are downloaded once and kept in the local cache.");
        vita2d_pgf_draw_text(font,x+28,y+118,DIM,.54f,"This can take a very long time and use network data.");
        const unsigned WARN=RGBA8(0xFF,0xB0,0x20,255);
        vita2d_draw_rectangle(x+24,y+136,w-48,36,RGBA8(0x3A,0x2A,0x10,255));
        vita2d_draw_rectangle(x+24,y+136,3,36,WARN);
        vita2d_pgf_draw_text(font,x+36,y+150,WARN,.56f,"Warning: total may exceed 2 GB");
        vita2d_pgf_draw_text(font,x+36,y+166,WARN,.50f,"of data. Use Wi-Fi and check free space.");
        char count[96];
        sceClibSnprintf(count,sizeof(count),"Pending images: %u",(unsigned)totalImages);
        vita2d_pgf_draw_text(font,x+28,y+188,ACCENT,.68f,count);
        vita2d_pgf_draw_text(font,x+28,y+210,TEXT,.52f,"Already cached images are excluded from this count.");
        vita2d_pgf_draw_text(font,x+28,y+228,TEXT,.52f,"Speed, ETA and progress appear in the next panel.");
        vita2d_draw_rectangle(btn0x,by,bw,bh,selected==0?ACCENT:SURFACE);
        vita2d_draw_rectangle(btn1x,by,bw,bh,selected==1?ACCENT:SURFACE);
        vita2d_pgf_draw_text(font,x+125,by+25,selected==0?BLACK:WHITE,.62f,"DOWNLOAD ALL");
        vita2d_pgf_draw_text(font,x+425,by+25,selected==1?BLACK:WHITE,.62f,"LATER");
        vita2d_pgf_draw_text(font,x+28,y+h-14,DIM,.52f,"Touch buttons or  Left/Right + Cross / Circle");
        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceKernelDelayThread(16*1000);
    }
    vita2d_wait_rendering_done();
    vita2d_free_pgf(font);
    return yes;
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

std::string formatBytes(uint64_t b){char o[64];double v=(double)b;if(b>=1024ULL*1024ULL*1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f GB",v/(1024.0*1024.0*1024.0));else if(b>=1024ULL*1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f MB",v/(1024.0*1024.0));else if(b>=1024ULL)sceClibSnprintf(o,sizeof(o),"%.2f KB",v/1024.0);else sceClibSnprintf(o,sizeof(o),"%llu B",(unsigned long long)b);return o;}
std::string twoLineFileName(const std::string&name){if(name.size()<=42)return name;if(name.size()<=84)return name.substr(0,42)+"\n"+name.substr(42);return name.substr(0,42)+"\n..."+name.substr(name.size()-38);}
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
struct StartupImageJob{std::string url;std::string path;std::string namespaceName;std::string fileName;};
void addStartupImage(std::vector<StartupImageJob>&jobs,std::unordered_set<std::string>&seen,psvitaalive::ui::ImageCache&images,const std::string&url,const std::string&namespaceName,bool queueNow){if(url.empty()||images.isCached(url,namespaceName))return;const std::string key=namespaceName+"\n"+url;if(!seen.insert(key).second)return;std::string path;if(queueNow)path=images.request(url,namespaceName);jobs.push_back({url,path,namespaceName,fileNameFromUrl(url,namespaceName+"_image")});}
void collectCatalogImages(std::vector<StartupImageJob>&jobs,std::unordered_set<std::string>&seen,psvitaalive::ui::ImageCache&images,const std::vector<psvitaalive::ui::CatalogItem>&items,bool queueNow){for(const auto&item:items){addStartupImage(jobs,seen,images,item.icon,"app",queueNow);addStartupImage(jobs,seen,images,item.cover,"app",queueNow);const size_t count=std::min<size_t>(5,item.screenshots.size());for(size_t i=0;i<count;++i)addStartupImage(jobs,seen,images,item.screenshots[i],"shot",queueNow);}}
void queueStartupImages(std::vector<StartupImageJob>&jobs,psvitaalive::ui::ImageCache&images){for(auto&job:jobs){if(job.path.empty())job.path=images.request(job.url,job.namespaceName);}}
void imageWarmupProgress(const std::vector<StartupImageJob>&jobs,psvitaalive::ui::ImageCache&images,uint64_t&completed,std::string&currentFile,bool&failedCurrent){completed=0;currentFile.clear();failedCurrent=false;for(const auto&job:jobs){const bool ready=images.isReady(job.path),failed=images.isFailed(job.path);if(ready||failed){++completed;continue;}if(currentFile.empty())currentFile=job.fileName;}}
bool runImageWarmup(std::vector<StartupImageJob>&jobs,psvitaalive::ui::ImageCache&images){
    if(jobs.empty())return true;
    images.cancelAll();
    while(images.progress().active)sceKernelDelayThread(10*1000);
    images.resetProgress();vita2d_pgf*prepFont=vita2d_load_default_pgf();if(prepFont){vita2d_start_drawing();vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,96));const int pw=620,ph=280,px=(960-pw)/2,py=(544-ph)/2;vita2d_draw_rectangle(px,py,pw,ph,RGBA8(0x20,0x20,0x20,255));vita2d_draw_rectangle(px,py,pw,2,RGBA8(0x3B,0xFF,0,255));vita2d_pgf_draw_text(prepFont,px+28,py+44,RGBA8(0x3B,0xFF,0,255),.68f,"PSVitaAlive");vita2d_pgf_draw_text(prepFont,px+28,py+86,RGBA8(255,255,255,255),1.0f,"Preparing image downloads");vita2d_pgf_draw_text(prepFont,px+28,py+122,RGBA8(0xAA,0xAA,0xAA,255),.58f,"Building the download queue...");vita2d_pgf_draw_text(prepFont,px+28,py+160,RGBA8(0x3B,0xFF,0,255),.62f,"Please wait");vita2d_end_drawing();vita2d_swap_buffers();sceKernelDelayThread(16*1000);vita2d_free_pgf(prepFont);}
    queueStartupImages(jobs,images);
    vita2d_pgf* font=vita2d_load_default_pgf();
    if(!font){images.cancelAll();return false;}
    const uint64_t started=sceKernelGetSystemTimeWide();uint64_t lastPoll=0;bool cancelled=false;
    while(true){
        const uint64_t now=sceKernelGetSystemTimeWide();
        uint64_t completed=0;std::string currentFile;bool failedCurrent=false;imageWarmupProgress(jobs,images,completed,currentFile,failedCurrent);
        const auto p=images.progress();
        if(completed>=(uint64_t)jobs.size()&&!p.active)break;
        SceCtrlData pad={};sceCtrlPeekBufferPositive(0,&pad,1);if(pad.buttons&SCE_CTRL_CIRCLE&&!cancelled){images.cancelAll();cancelled=true;}if(cancelled){vita2d_start_drawing();vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,96));vita2d_draw_rectangle(170,142,620,260,RGBA8(0x20,0x20,0x20,255));vita2d_draw_rectangle(170,142,620,2,RGBA8(0x3B,0xFF,0,255));vita2d_pgf_draw_text(font,198,184,RGBA8(0x3B,0xFF,0,255),.68f,"PSVitaAlive");vita2d_pgf_draw_text(font,198,226,RGBA8(255,255,255,255),1.0f,"Cancelling image downloads");vita2d_pgf_draw_text(font,198,262,RGBA8(0xAA,0xAA,0xAA,255),.58f,"Stopping transfer and removing incomplete file...");vita2d_end_drawing();vita2d_swap_buffers();if(!images.progress().active)break;sceKernelDelayThread(16*1000);continue;}
        if(now<lastPoll){sceKernelDelayThread(16*1000);continue;}lastPoll=now+100000;
        const unsigned SURFACE=RGBA8(0x37,0x37,0x37,255),BORDER=RGBA8(0x6E,0x6E,0x6E,255),TEXT=RGBA8(0xAA,0xAA,0xAA,255),DIM=RGBA8(0x6E,0x6E,0x6E,255),ACCENT=RGBA8(0x3B,0xFF,0,255),WHITE=RGBA8(255,255,255,255),BLACK=RGBA8(0,0,0,255),PANEL=RGBA8(0x20,0x20,0x20,255);
        const int w=620,h=320,x=(960-w)/2,y=(544-h)/2;
        vita2d_start_drawing();vita2d_draw_rectangle(0,0,960,544,RGBA8(0,0,0,96));vita2d_draw_rectangle(x,y,w,h,PANEL);vita2d_draw_rectangle(x,y,w,2,ACCENT);vita2d_draw_rectangle(x,y+2,2,h-2,ACCENT);
        vita2d_pgf_draw_text(font,x+28,y+32,ACCENT,.68f,"PSVitaAlive");vita2d_pgf_draw_text(font,x+28,y+68,WHITE,1.02f,"Downloading Images");
        std::string fn=twoLineFileName(p.fileName.empty()?currentFile:p.fileName);const size_t nl=fn.find('\n');if(nl==std::string::npos)vita2d_pgf_draw_text(font,x+28,y+98,TEXT,.62f,fn.c_str());else{vita2d_pgf_draw_text(font,x+28,y+98,TEXT,.62f,fn.substr(0,nl).c_str());vita2d_pgf_draw_text(font,x+28,y+120,TEXT,.62f,fn.substr(nl+1).c_str());}
        const uint64_t current=p.downloaded,total=p.total;const uint64_t filePct=total?std::min<uint64_t>(100,(current*100)/total):0;const uint64_t overallPct=jobs.empty()?100:std::min<uint64_t>(100,((completed*10000)+(filePct*100))/jobs.size()/100);int bx=x+28,by=y+140,bw=w-56,bh=12;vita2d_draw_rectangle(bx,by,bw,bh,BORDER);vita2d_draw_rectangle(bx,by,bw*(int)overallPct/100,bh,ACCENT);char stats[220]={};sceClibSnprintf(stats,sizeof(stats),"%llu%%  Files: %llu / %u  •  %s/s",(unsigned long long)overallPct,(unsigned long long)completed,(unsigned)jobs.size(),formatBytes(p.speed).c_str());vita2d_pgf_draw_text(font,x+28,y+168,TEXT,.58f,stats);uint64_t eta=0;if(p.speed>0){uint64_t avg=completed?p.completedBytes/completed:(p.total?p.total:0);uint64_t futureFiles=jobs.size()>completed+(p.active?1:0)?jobs.size()-completed-(p.active?1:0):0;uint64_t remaining=(p.total>p.downloaded?p.total-p.downloaded:0)+avg*futureFiles;eta=remaining/p.speed;}char etaText[96];sceClibSnprintf(etaText,sizeof(etaText),"ETA: %s",formatEta(eta).c_str());vita2d_pgf_draw_text(font,x+28,y+194,ACCENT,.62f,etaText);
        char known[220]={};sceClibSnprintf(known,sizeof(known),"Known size: %s",formatBytes(p.knownTotalBytes).c_str());vita2d_pgf_draw_text(font,x+28,y+218,TEXT,.56f,known);
        vita2d_draw_rectangle(x+w-190,y+h-52,162,38,cancelled?ACCENT:SURFACE);vita2d_draw_rectangle(x+w-190,y+h-52,162,1,ACCENT);vita2d_pgf_draw_text(font,x+w-176,y+h-27,cancelled?BLACK:WHITE,.54f,"CIRCLE  CANCEL DOWNLOAD");
        vita2d_end_drawing();vita2d_swap_buffers();sceKernelDelayThread(16*1000);
        if(cancelled&&!images.progress().active)break;
    }
    vita2d_wait_rendering_done();vita2d_free_pgf(font);return !cancelled;
}
std::string progressMessage(uint64_t current,uint64_t total,const std::string&prefix,const std::string&file){char b[320];const uint64_t pct=total?std::min<uint64_t>(100,(current*100)/total):0;sceClibSnprintf(b,sizeof(b),"%s | %llu%% | %llu / %llu%s%s",prefix.c_str(),(unsigned long long)pct,(unsigned long long)current,(unsigned long long)total,file.empty()?"":" | ",file.c_str());return b;}
}

int main(){
    psvitaalive::diagnostics::init();
    psvitaalive::diagnostics::log("============================================================");
    psvitaalive::diagnostics::log("PSVitaAlive session BEGIN");
    psvitaalive::diagnostics::log("TitleID=PSVAS1178");

    // Required for system dialogs (IME keyboard) on real hardware — VitaDB does this.
    {
        SceAppUtilInitParam appUtilParam;
        SceAppUtilBootParam appUtilBootParam;
        sceClibMemset(&appUtilParam, 0, sizeof(appUtilParam));
        sceClibMemset(&appUtilBootParam, 0, sizeof(appUtilBootParam));
        const int appUtilRes = sceAppUtilInit(&appUtilParam, &appUtilBootParam);
        if (appUtilRes < 0) {
            psvitaalive::diagnostics::log("[System] sceAppUtilInit failed (IME may not work)");
        }

        SceCommonDialogConfigParam cmnDlgCfgParam;
        sceCommonDialogConfigParamInit(&cmnDlgCfgParam);
        sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, (int*)&cmnDlgCfgParam.language);
        sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, (int*)&cmnDlgCfgParam.enterButtonAssign);
        sceCommonDialogSetConfigParam(&cmnDlgCfgParam);
        psvitaalive::diagnostics::log("[System] AppUtil + CommonDialog initialized for IME");
    }

    psvitaalive::StorageManager storage;storage.initProjectDirs();
    psvitaalive::InstallController installer;psvitaalive::CatalogManager catalogs;psvitaalive::ui::ImageCache images;
    if(!installer.init())psvitaalive::diagnostics::log("[System] InstallController init failed");

    psvitaalive::ui::FullCatalogScreen screen;screen.setImageCache(&images);
    screen.setCatalogChangeCallback([&](psvitaalive::ui::CatalogType next){psvitaalive::diagnostics::log(std::string("[UI] catalog requested: ")+psvitaalive::ui::catalogName(next));images.cancelQueuedRequests();return catalogs.request(next);});
    screen.setSearchCallback([&](const std::string&current){std::string result=current;if(promptText(current,"Search catalog",result))return result;return current;});
    screen.setInstallCancelCallback([&installer](){ installer.cancel(); });
    screen.setInstallAcknowledgeCallback([&installer](){ installer.acknowledgeResult(); });
    screen.setInstallCallbacks([&installer](const psvitaalive::ui::CatalogItem&item){psvitaalive::diagnostics::log("[UI] INSTALL REQUEST name="+item.name+" title_id="+item.titleId+" url="+item.downloadUrl);
    if(item.downloadUrl.empty()||item.downloadFileName.empty())return false;
        std::string zrif;std::string ltype;std::string cid;std::string extractPath;
        for(const auto&L:item.linkDetails){if(L.url==item.downloadUrl){zrif=L.zrif;ltype=L.type;cid=L.contentId;extractPath=L.extractPath;break;}}
        std::string zipDestination;
        if(isZipName(item.downloadFileName)){
            if(!extractPath.empty()){
                zipDestination=extractPath;
                psvitaalive::diagnostics::log(std::string("[UI] ZIP extract_path from catalog: ")+zipDestination);
            }else{
                zipDestination="ux0:data/";
                if(!promptZipDestination(zipDestination)){psvitaalive::diagnostics::log("[UI] ZIP destination cancelled");return false;}
            }
        }
        {
            uint64_t exp = parseCatalogSizeBytes(item.size);
            return installer.requestInstall(item.downloadUrl,item.downloadFileName,zipDestination,zrif,ltype,cid,item.name,exp);
        }},[&installer](){return installStatusText(installer.status());});
    screen.setLinkActionCallback([&installer](const psvitaalive::ui::CatalogItem&item,const psvitaalive::ui::CatalogLink&link){
        const std::string type=link.type;
        const bool actionable=(type=="Download"||type=="download"||type=="Downloads"||type=="Mirror"||type=="mirror"||type=="DLC"||type=="dlc"||link.url.find(".vpk")!=std::string::npos||link.url.find(".pkg")!=std::string::npos||link.url.find(".zip")!=std::string::npos||link.url.find(".pbp")!=std::string::npos||link.url.find(".iso")!=std::string::npos||link.url.find(".cso")!=std::string::npos);
        if(!actionable){psvitaalive::diagnostics::log(std::string("[UI] link is informational only: ")+link.url);return false;}
        psvitaalive::ui::CatalogItem requestItem=item;requestItem.downloadUrl=link.url;requestItem.downloadFileName=fileNameFromUrl(link.url,item.id);
        psvitaalive::diagnostics::log(std::string("[UI] LINK INSTALL name=")+item.name+" type="+link.type+" url="+link.url);
        std::string zipDestination;
        if(isZipName(requestItem.downloadFileName)){
            // Prefer per-link extract_path when present; otherwise ask the user.
            if(!link.extractPath.empty()){
                zipDestination=link.extractPath;
                psvitaalive::diagnostics::log(std::string("[UI] ZIP extract_path from catalog: ")+zipDestination);
            }else{
                zipDestination="ux0:data/";
                if(!promptZipDestination(zipDestination)){
                    psvitaalive::diagnostics::log("[UI] ZIP destination cancelled");
                    return false;
                }
            }
        }
        {
            uint64_t exp = parseCatalogSizeBytes(link.size);
            if (exp == 0) exp = parseCatalogSizeBytes(item.size);
            return installer.requestInstall(requestItem.downloadUrl,requestItem.downloadFileName,zipDestination,link.zrif,link.type,link.contentId,item.name,exp);
        }
    });

    if(!screen.init()){psvitaalive::diagnostics::log("[System] UI initialization failed");installer.shutdown();catalogs.shutdown();images.shutdown();psvitaalive::diagnostics::shutdown();sceKernelExitProcess(1);return 1;}

    // VitaShell pattern: remove temporary self-update helper bubble if present.
    psvitaalive::UpdateChecker::cleanupUpdaterBubble();

    if (installer.settings().startupUpdateCheck) {
        psvitaalive::diagnostics::log("[Startup] update check enabled by config");
        psvitaalive::StartupUpdateManager startupUpdate;
        screen.setCatalogLoading(true, "Startup Update", 0, 0, "Checking for application updates...");
        if(!startupUpdate.start(PSVITAALIVE_VERSION)){
            screen.showToast("Update startup unavailable — continuing", 1800);
        }else{
            while(startupUpdate.isBusy()){
                const auto us = startupUpdate.snapshot();
                screen.setCatalogLoading(true, "Startup Update", us.current, us.total,
                                          us.message.empty()?"Updating application...":us.message);
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
                screen.setCatalogLoading(true, "Self-update", 1, 1, "Starting updater...");
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
                screen.showToast("Update unavailable — continuing", 1800);
            else if(us.state == psvitaalive::StartupUpdateManager::State::Cancelled)
                screen.showToast("Update cancelled — continuing", 1600);
        }
    } else {
        psvitaalive::diagnostics::log("[Startup] update check disabled by config");
    }

    psvitaalive::diagnostics::log("[Startup] update phase finished — starting catalogs");

    // Catalog and image workers do not exist until the startup update phase is finished.
    if(!catalogs.init())psvitaalive::diagnostics::log("[System] CatalogManager init failed");
    if(!images.init())psvitaalive::diagnostics::log("[System] ImageCache init failed");
    screen.setImageCache(&images);

    const int catalogCount=(int)psvitaalive::ui::CatalogType::Count;
    int preloadIndex=0;bool startupCatalogs=true;bool homebrewReady=false;bool startupImageChoicePending=false;bool startupNewsPending=false;
    std::vector<std::vector<psvitaalive::ui::CatalogItem>> startupCatalogItems((size_t)catalogCount);
    std::vector<StartupImageJob> startupImagesJobs;std::unordered_set<std::string> startupImageSeen;

    screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);
    screen.setCatalogLoading(true,psvitaalive::ui::catalogName(psvitaalive::ui::CatalogType::Homebrew),0,0,"Checking catalog cache...");
    if(!catalogs.request(psvitaalive::ui::CatalogType::Homebrew)){startupCatalogs=false;screen.setCatalogError("Unable to start catalog check");}

        
    screen.setAppSettings(installer.settings());
    screen.setPluginStatus(installer.plugins());
    screen.setSettingsSaveCallback([&installer, &screen](const psvitaalive::AppSettingsData& s) {
        installer.setSettings(s);
        screen.setAppSettings(s);
    });

    // Plugin warnings (settings.warn_missing_plugins)
    if (installer.settings().startupPluginDetection && installer.settings().warnMissingPlugins) {
        const auto& pl = installer.plugins();
        if (!pl.nonpdrm) {
            screen.showToast("NoNpDrm not found: licensed Vita PKGs may fail", 3500);
        } else if (!pl.nopspemudrmKern) {
            screen.showToast("NoPspEmuDrm not found: PSP via Adrenaline only", 3200);
        }
    }

while(screen.updateAndDraw()){
        const uint64_t now=sceKernelGetSystemTimeWide();
        psvitaalive::CatalogManager::Status cs=catalogs.status();
        if(startupCatalogs&&cs.state==psvitaalive::CatalogManager::State::Loading){screen.setCatalogLoading(true,cs.label,cs.current,cs.total,progressMessage(cs.current,cs.total,cs.message,""));}
        else if(startupCatalogs&&cs.state==psvitaalive::CatalogManager::State::Failed){
            psvitaalive::diagnostics::log(std::string("[Startup] catalog failed: ")+cs.label+" error="+cs.error);
            ++preloadIndex;
            if(preloadIndex<catalogCount){const auto next=(psvitaalive::ui::CatalogType)preloadIndex;screen.setCatalogLoading(true,psvitaalive::ui::catalogName(next),0,0,"Checking next catalog cache...");catalogs.request(next);}
            else{
                startupCatalogs=false;startupImageChoicePending=true;startupImagesJobs.clear();startupImageSeen.clear();for(const auto&items:startupCatalogItems)collectCatalogImages(startupImagesJobs,startupImageSeen,images,items,false);screen.setCatalogLoading(false,"",0,(uint64_t)startupImagesJobs.size(),"Catalogs ready");psvitaalive::diagnostics::log("[Startup] all catalogs processed; waiting for image warmup choice");
            }
        }

        std::vector<psvitaalive::ui::CatalogItem> ready;psvitaalive::ui::CatalogType readyCatalog;
        if(catalogs.takeReady(ready,readyCatalog)){
            psvitaalive::diagnostics::log(std::string("[System] catalog ready: ")+psvitaalive::ui::catalogName(readyCatalog));
            startupCatalogItems[(int)readyCatalog]=ready;
            if(startupCatalogs){
                if(readyCatalog==psvitaalive::ui::CatalogType::Homebrew){
                    screen.setCatalogItems(ready);
                    screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);
                    homebrewReady=true;
                }
                ++preloadIndex;
                if(preloadIndex<catalogCount){const auto next=(psvitaalive::ui::CatalogType)preloadIndex;screen.setCatalogLoading(true,psvitaalive::ui::catalogName(next),0,0,"Checking next catalog cache...");catalogs.request(next);}
                else{startupCatalogs=false;startupImageChoicePending=true;startupImagesJobs.clear();startupImageSeen.clear();for(const auto&items:startupCatalogItems)collectCatalogImages(startupImagesJobs,startupImageSeen,images,items,false);screen.setCatalogLoading(false,"",0,(uint64_t)startupImagesJobs.size(),"Catalogs ready");psvitaalive::diagnostics::log("[Startup] all catalogs ready; waiting for image warmup choice");}
            }else{
                // Only clear loading if nothing else is still in flight (e.g. user
                // already requested another tab before this result was published).
                screen.setCatalogItems(std::move(ready));
                screen.setActiveCatalog(readyCatalog);
                if(catalogs.isBusy()){
                    screen.setCatalogLoading(true,psvitaalive::ui::catalogName(readyCatalog),0,0,"Loading next catalog...");
                }else{
                    screen.setCatalogLoading(false,psvitaalive::ui::catalogName(readyCatalog),1,1,"Ready");
                }
            }
        }

        if(startupImageChoicePending){
            startupImageChoicePending=false;
            if(startupImagesJobs.empty()){
                screen.setCatalogLoading(false,"",0,0,"All catalog images are already cached");psvitaalive::diagnostics::log("[Startup] no pending catalog images");startupNewsPending=true;
                if(homebrewReady){screen.setCatalogItems(std::move(startupCatalogItems[(int)psvitaalive::ui::CatalogType::Homebrew]));screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);}
                // Drop the other preloaded catalogs from RAM (Vita Games + zRIF was huge).
                for(auto& bucket : startupCatalogItems){ std::vector<psvitaalive::ui::CatalogItem> empty; bucket.swap(empty); }
            }else if(installer.settings().promptImageWarmup && promptDownloadAllImages(startupImagesJobs.size())){
                const bool completed=runImageWarmup(startupImagesJobs,images);screen.setCatalogLoading(false,"",completed?startupImagesJobs.size():0,(uint64_t)startupImagesJobs.size(),completed?"Image cache ready":"Image download cancelled");psvitaalive::diagnostics::log(std::string("[Startup] full image warmup finished result=")+(completed?"COMPLETE":"CANCELLED"));startupNewsPending=true;
                if(homebrewReady){screen.setCatalogItems(std::move(startupCatalogItems[(int)psvitaalive::ui::CatalogType::Homebrew]));screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);}
                // Drop the other preloaded catalogs from RAM (Vita Games + zRIF was huge).
                for(auto& bucket : startupCatalogItems){ std::vector<psvitaalive::ui::CatalogItem> empty; bucket.swap(empty); }
            }else{
                // User declined once (or prompt disabled in settings): remember and stay on-demand.
                if(installer.settings().promptImageWarmup){
                    auto s=installer.settings();
                    s.promptImageWarmup=false;
                    installer.setSettings(s);
                    screen.setAppSettings(s);
                    psvitaalive::diagnostics::log("[Startup] image warmup declined - will not ask again (enable in Settings)");
                }

                screen.setCatalogLoading(false,"",0,0,"Images will download while browsing");startupImagesJobs.clear();startupImageSeen.clear();psvitaalive::diagnostics::log("[Startup] user selected on-demand image loading");startupNewsPending=true;
                if(homebrewReady){screen.setCatalogItems(std::move(startupCatalogItems[(int)psvitaalive::ui::CatalogType::Homebrew]));screen.setActiveCatalog(psvitaalive::ui::CatalogType::Homebrew);}
                // Drop the other preloaded catalogs from RAM (Vita Games + zRIF was huge).
                for(auto& bucket : startupCatalogItems){ std::vector<psvitaalive::ui::CatalogItem> empty; bucket.swap(empty); }
            }
        }

        
        // Keep L/R locked for the whole CatalogManager busy window (not only startup).
        if(!startupCatalogs){
            const auto csBusy=catalogs.status();
            if(catalogs.isBusy()||csBusy.state==psvitaalive::CatalogManager::State::Loading){
                screen.setCatalogLoading(true,
                    csBusy.label.empty()?psvitaalive::ui::catalogName(csBusy.catalog):csBusy.label,
                    csBusy.current,csBusy.total,
                    csBusy.message.empty()?"Loading catalog...":csBusy.message);
            }
        }

const psvitaalive::InstallStatus cur=installer.status();using InstallState=psvitaalive::InstallStatus::State;images.setNetworkPaused(cur.state==InstallState::Downloading||cur.state==InstallState::Installing);const bool active=cur.state==InstallState::Downloading||cur.state==InstallState::Installing||cur.state==InstallState::Completed||cur.state==InstallState::Failed||cur.state==InstallState::Cancelled;int outcome=0;if(cur.state==InstallState::Completed)outcome=1;else if(cur.state==InstallState::Cancelled)outcome=3;else if(cur.state==InstallState::Failed)outcome=2;screen.setInstallProgress(active,cur.current,cur.total,cur.bytesPerSecond,cur.stage,cur.fileName,cur.message,outcome,cur.liveAreaOk,cur.installPath,cur.titleId,cur.resultAutoCloseRemainingMs);
        if(startupNewsPending && !active && !screen.isNewsVisible()){
            screen.runNewsCheck(false);
            if(screen.isNewsCheckDone() || screen.isNewsVisible()){
                startupNewsPending=false;
            }
        }
    }

    screen.setInstallProgress(false,0,0,0,"","","",0,false,"","");screen.shutdown();installer.shutdown();catalogs.shutdown();images.shutdown();psvitaalive::diagnostics::log("PSVitaAlive session END");psvitaalive::diagnostics::shutdown();sceKernelExitProcess(0);return 0;
}
