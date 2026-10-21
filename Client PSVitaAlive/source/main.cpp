/* PSVitaAlive - native client entry point. */
#include "localization/localization.hpp"
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
#include <vector>
#include "diagnostic_logger.hpp"
#include "ime_diagnostics.hpp"
#include "storage/storage_manager.hpp"
#include "installer/install_controller.hpp"
#include "installer/install_archive_policy.hpp"
#include "ui/ui_font.hpp"
#include "ui/full_catalog_screen.hpp"
#include "ui/image_cache.hpp"
#include "catalog/catalog_manager.hpp"
#include "catalog/catalog_trace.hpp"
#include "update/startup_update_manager.hpp"
#include "update/update_checker.hpp"
#include "installer/install_space_policy.hpp"

namespace {
std::string installStatusText(const psvitaalive::InstallStatus&s){using S=psvitaalive::InstallStatus::State;if(s.state==S::Idle)return{};char b[384];uint64_t p=s.total?std::min<uint64_t>(100,(s.current*100)/s.total):0;sceClibSnprintf(b,sizeof(b),"%s | %s | %llu%% | %s",s.stage.c_str(),s.fileName.empty()?"file":s.fileName.c_str(),(unsigned long long)p,s.message.c_str());return b;}
size_t utf8ToUtf16(const std::string& src, SceWChar16* dst, size_t cap) {
    if (!dst || cap == 0) return 0;
    size_t i = 0, out = 0;
    while (i < src.size() && out + 1 < cap) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        uint32_t cp = 0xFFFD;
        size_t step = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < src.size()) {
            const unsigned char c1 = static_cast<unsigned char>(src[i + 1]);
            if ((c1 & 0xC0) == 0x80) {
                cp = ((c & 0x1F) << 6) | (c1 & 0x3F);
                if (cp >= 0x80) step = 2; else cp = 0xFFFD;
            }
        } else if ((c & 0xF0) == 0xE0 && i + 2 < src.size()) {
            const unsigned char c1 = static_cast<unsigned char>(src[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(src[i + 2]);
            if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80) {
                cp = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
                if (cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF)) step = 3; else cp = 0xFFFD;
            }
        } else if ((c & 0xF8) == 0xF0 && i + 3 < src.size()) {
            const unsigned char c1 = static_cast<unsigned char>(src[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(src[i + 2]);
            const unsigned char c3 = static_cast<unsigned char>(src[i + 3]);
            if ((c1 & 0xC0) == 0x80 && (c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
                cp = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                if (cp >= 0x10000 && cp <= 0x10FFFF) step = 4; else cp = 0xFFFD;
            }
        }
        if (cp <= 0xFFFF) {
            dst[out++] = static_cast<SceWChar16>(cp);
        } else {
            if (out + 2 >= cap) break;
            cp -= 0x10000;
            dst[out++] = static_cast<SceWChar16>(0xD800 | (cp >> 10));
            dst[out++] = static_cast<SceWChar16>(0xDC00 | (cp & 0x3FF));
        }
        i += step;
    }
    dst[out] = 0;
    return out;
}

std::string utf16ToUtf8(const SceWChar16* src) {
    std::string out;
    if (!src) return out;
    for (size_t i = 0; src[i] != 0 && i < 2048; ++i) {
        uint32_t cp = src[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && src[i + 1] >= 0xDC00 && src[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (src[++i] - 0xDC00);
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

class ImeController {
public:
    bool open(const std::string& initial, const std::string& title) {
        if (state_ != State::Idle) {
            psvitaalive::diagnostics::log("[UI] IME open blocked: previous session still cooling down");
            return false;
        }

        psvitaalive::ime_diagnostics::record("open_enter", sessionId_ + 1, (int)initial.size(), (int)title.size(), true);
        sceClibMemset(inputBuf_, 0, sizeof(inputBuf_));
        sceClibMemset(initialBuf_, 0, sizeof(initialBuf_));
        sceClibMemset(titleBuf_, 0, sizeof(titleBuf_));
        utf8ToUtf16(initial, initialBuf_, kMaxLen + 1);
        utf8ToUtf16(initial, inputBuf_, kMaxLen + 1);
        utf8ToUtf16(title.empty() ? std::string("Search") : title, titleBuf_, SCE_IME_DIALOG_MAX_TITLE_LENGTH + 1);

        SceImeDialogParam param;
        sceImeDialogParamInit(&param);
        param.supportedLanguages = 0x0001FFFF;
        // IME PAF crash A/B test: let the system choose its own keyboard language.
        // Keep supportedLanguages, type and all lifecycle code unchanged.
        param.languagesForced = SCE_FALSE;
        param.type = SCE_IME_TYPE_DEFAULT;
        param.option = SCE_IME_OPTION_NO_AUTO_CAPITALIZATION;
        param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
        param.textBoxMode = SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
        param.title = titleBuf_;
        param.maxTextLength = kMaxLen;
        param.initialText = initialBuf_;
        param.inputTextBuffer = inputBuf_;
        param.enterLabel = SCE_IME_ENTER_LABEL_SEARCH;

        resultReady_ = false;
        resultAccepted_ = false;
        resultText_.clear();
        cooldownFrames_ = 0;
        recoveryHoldStartUs_ = abortStartedUs_ = 0;
        recoveryRequested_ = previousButtonMask_ = buttonReadErrorLogged_ = false;
        statusPolls_ = 0;
        nextStatusHeartbeatUs_ = sceKernelGetSystemTimeWide() + 2000000ULL;
        ++sessionId_;
        psvitaalive::ime_diagnostics::setSession(sessionId_);
        psvitaalive::ime_diagnostics::record("init_before", sessionId_, (int)param.type, (int)param.languagesForced, true);

        // Do not explicitly load SCE_SYSMODULE_IME here. The official VitaSDK
        // sample, VitaShell and pkgj use sceImeDialogInit directly. Keeping the
        // lifecycle owned by CommonDialog avoids accumulating/competing module
        // state across many open/close cycles.
        const int r = sceImeDialogInit(&param);
        psvitaalive::ime_diagnostics::record("init_after", sessionId_, r, (int)param.maxTextLength, true);
        lastStatus_ = -1;
        char b[128];
        sceClibSnprintf(b, sizeof(b), "[UI] IME session=%u init=0x%08X", sessionId_, r);
        psvitaalive::diagnostics::log(b);
        if (r < 0) {
            state_ = State::Idle;
            return false;
        }

        state_ = State::Running;
        return true;
    }

    void poll() {
        if (state_ == State::Idle) return;

        // IMPORTANT: after sceImeDialogTerm(), do not call any sceImeDialog*
        // status/result function again until a new session is initialized.
        // VitaSDK's sample and long-lived apps stop polling the terminated IME.
        // We only keep servicing CommonDialog in the normal render loop and
        // wait a few frames before allowing another sceImeDialogInit().
        if (state_ == State::Cooldown) {
            if (++cooldownFrames_ >= kCooldownFrames) {
                state_ = State::Idle;
                cooldownFrames_ = 0;
                psvitaalive::ime_diagnostics::record("cooldown_complete", sessionId_, kCooldownFrames, 0, true);
                psvitaalive::diagnostics::log("[UI] IME cooldown complete");
            }
            return;
        }

        // FullCatalogScreen blocks ordinary controls; peek hardware buttons
        // directly so L+R+START can recover a hung native keyboard.
        pollRecoveryButtons();
        const uint64_t nowUs = sceKernelGetSystemTimeWide();
        const bool heartbeat = nowUs >= nextStatusHeartbeatUs_;
        if(heartbeat) ime_diagnosticsRecord("status_poll_before", (int)statusPolls_,
                                            recoveryRequested_ ? 1 : 0);
        const SceCommonDialogStatus st = sceImeDialogGetStatus();
        ++statusPolls_;
        if(heartbeat) {
            ime_diagnosticsRecord("status_poll_after", (int)st, (int)statusPolls_, true);
            nextStatusHeartbeatUs_ = nowUs + 2000000ULL;
        }
        if ((int)st != lastStatus_) {
            psvitaalive::ime_diagnostics::record("status_change", sessionId_, (int)st, lastStatus_, true);
            lastStatus_ = (int)st;
        }
        if (st == SCE_COMMON_DIALOG_STATUS_FINISHED) {
            psvitaalive::ime_diagnostics::record("get_result_before", sessionId_, 0, 0, true);
            SceImeDialogResult result{};
            const int gr = sceImeDialogGetResult(&result);
            psvitaalive::ime_diagnostics::record("get_result_after", sessionId_, gr, (int)result.button, true);
            resultAccepted_ = (!recoveryRequested_ && gr >= 0 && result.button == SCE_IME_DIALOG_BUTTON_ENTER);
            resultText_ = resultAccepted_ ? utf16ToUtf8(inputBuf_) : std::string();
            psvitaalive::ime_diagnostics::record("term_before", sessionId_, resultAccepted_ ? 1 : 0, (int)resultText_.size(), true);
            const int tr = sceImeDialogTerm();
            psvitaalive::ime_diagnostics::record("term_after", sessionId_, tr, resultAccepted_ ? 1 : 0, true);
            resultReady_ = true;
            state_ = State::Cooldown;
            cooldownFrames_ = 0;

            char b[160];
            sceClibSnprintf(b, sizeof(b),
                "[UI] IME session=%u finished get=0x%08X term=0x%08X accepted=%d",
                sessionId_, gr, tr, resultAccepted_ ? 1 : 0);
            psvitaalive::diagnostics::log(b);
        } else if (st == SCE_COMMON_DIALOG_STATUS_NONE) {
            // An initialized dialog unexpectedly disappearing is treated as a
            // cancelled session. Do not call Term/GetStatus again; let the
            // CommonDialog renderer settle before another initialization.
            resultAccepted_ = false;
            resultText_.clear();
            resultReady_ = true;
            state_ = State::Cooldown;
            cooldownFrames_ = 0;
            psvitaalive::ime_diagnostics::record("unexpected_status_none", sessionId_, 0, 0, true);
            psvitaalive::diagnostics::log("[UI] IME returned NONE while running; entering cooldown");
        }
        if (state_ == State::Running && recoveryRequested_ && abortStartedUs_ != 0 &&
            sceKernelGetSystemTimeWide() - abortStartedUs_ >= 8000000ULL) {
            // Do not call Term while the SDK still reports RUNNING.
            // Exit after logging if the firmware ignores the abort request.
            ime_diagnosticsRecord("abort_timeout_exit", (int)st, (int)statusPolls_, true);
            psvitaalive::diagnostics::log("[IME] abort failed to finish; emergency app exit");
            psvitaalive::diagnostics::flush();
            psvitaalive::ime_diagnostics::shutdown();
            sceKernelExitProcess(0);
        }
    }

    bool takeFinished(std::string& text, bool& accepted) {
        if (!resultReady_ || state_ != State::Idle) return false;
        text = resultText_;
        accepted = resultAccepted_;
        resultReady_ = false;
        return true;
    }

    bool busy() const { return state_ != State::Idle; }
    unsigned nextSessionId() const { return sessionId_ + 1; }

    bool runBlocking(const std::string& initial, const std::string& title, std::string& out) {
        if (!open(initial, title)) return false;
        int frames = 0;
        bool abortRequested = false;
        while (busy()) {
            vita2d_start_drawing();
            vita2d_clear_screen();
            vita2d_draw_rectangle(0, 0, 960, 544, RGBA8(0, 0, 0, 180));
            vita2d_end_drawing();
            vita2d_common_dialog_update();
            vita2d_swap_buffers();
            poll();
            if (!abortRequested && state_ == State::Running && ++frames > 60 * 180) {
                const int ar = sceImeDialogAbort();
                char b[96];
                sceClibSnprintf(b, sizeof(b), "[UI] IME blocking timeout abort=0x%08X", ar);
                psvitaalive::diagnostics::log(b);
                abortRequested = true;
            }
            sceKernelDelayThread(16 * 1000);
        }
        std::string text;
        bool accepted = false;
        if (!takeFinished(text, accepted)) return false;
        if (accepted) out = text;
        return accepted;
    }

    void shutdown() {
        // Only query the IME while this controller still owns a running
        // session. Never query it again after sceImeDialogTerm().
        if (state_ == State::Running) {
            const SceCommonDialogStatus st = sceImeDialogGetStatus();
            if (st == SCE_COMMON_DIALOG_STATUS_RUNNING) {
                const int ar = sceImeDialogAbort();
                char b[96];
                sceClibSnprintf(b, sizeof(b), "[UI] IME shutdown abort=0x%08X", ar);
                psvitaalive::diagnostics::log(b);
            } else if (st == SCE_COMMON_DIALOG_STATUS_FINISHED) {
                sceImeDialogTerm();
            }
        }
        state_ = State::Idle;
        resultReady_ = false;
        resultAccepted_ = false;
        cooldownFrames_ = 0;
    }

private:
    void ime_diagnosticsRecord(const char* name, int a = 0, int b = 0, bool memory = false) {
        psvitaalive::ime_diagnostics::record(name, sessionId_, a, b, memory);
    }

    void pollRecoveryButtons() {
        if (state_ != State::Running || recoveryRequested_) return;
        SceCtrlData pad{};
        const int read = sceCtrlPeekBufferPositive(0, &pad, 1);
        if (read < 1) {
            if (read < 0 && !buttonReadErrorLogged_) {
                buttonReadErrorLogged_ = true;
                ime_diagnosticsRecord("buttons_read_error", read);
            }
            recoveryHoldStartUs_ = 0;
            previousButtonMask_ = false;
            return;
        }
        const uint32_t combo = SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER | SCE_CTRL_START;
        const bool held = (pad.buttons & combo) == combo;
        const uint64_t nowUs = sceKernelGetSystemTimeWide();
        if (!held) {
            if (previousButtonMask_) ime_diagnosticsRecord("recovery_buttons_released");
            previousButtonMask_ = false;
            recoveryHoldStartUs_ = 0;
            return;
        }
        if (!previousButtonMask_) {
            recoveryHoldStartUs_ = nowUs;
            previousButtonMask_ = true;
            ime_diagnosticsRecord("recovery_buttons_held", read);
        }
        if (nowUs - recoveryHoldStartUs_ < 2000000ULL) return;
        ime_diagnosticsRecord("abort_before", lastStatus_, 0, true);
        const int ar = sceImeDialogAbort();
        ime_diagnosticsRecord("abort_after", ar, 0, true);
        psvitaalive::diagnostics::log("[IME] manual recovery L+R+START requested");
        recoveryRequested_ = true;
        abortStartedUs_ = sceKernelGetSystemTimeWide();
    }

    enum class State { Idle, Running, Cooldown };
    static constexpr SceUInt32 kMaxLen = 128;
    // The official sample effectively gives CommonDialog at least one update
    // after Term before reinitializing. Four frames (~64 ms at 60 Hz) add a
    // conservative margin without touching the terminated IME API.
    static constexpr int kCooldownFrames = 4;
    State state_ = State::Idle;
    bool resultReady_ = false;
    bool resultAccepted_ = false;
    unsigned sessionId_ = 0;
    int cooldownFrames_ = 0;
    int lastStatus_ = -1;
    uint64_t nextStatusHeartbeatUs_ = 0;
    uint64_t recoveryHoldStartUs_ = 0;
    uint64_t abortStartedUs_ = 0;
    unsigned statusPolls_ = 0;
    bool recoveryRequested_ = false;
    bool previousButtonMask_ = false;
    bool buttonReadErrorLogged_ = false;
    std::string resultText_;
    SceWChar16 inputBuf_[kMaxLen + 1]{};
    SceWChar16 initialBuf_[kMaxLen + 1]{};
    SceWChar16 titleBuf_[SCE_IME_DIALOG_MAX_TITLE_LENGTH + 1]{};
};

ImeController gIme;

bool promptText(const std::string& initial, const std::string& title, std::string& out) {
    return gIme.runBlocking(initial, title, out);
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
    psvitaalive::ime_diagnostics::init();
    psvitaalive::diagnostics::log("============================================================");
    psvitaalive::diagnostics::log("PSVitaAlive session BEGIN");
    PSVA_TRACE_EVENT("build_marker","event-driven-ux0-space-20261012 main=untouched fix=1");
    const uint64_t catTraceStarted=psvitaalive::catalog_trace::nowUs();
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
    if (!psvitaalive::LocalizationManager::instance().initialize(installer.settings())) {
        psvitaalive::diagnostics::log("[System] LocalizationManager initialize failed — English fallback");
    } else {
        psvitaalive::diagnostics::log(std::string("[System] UI language=") +
            psvitaalive::LocalizationManager::instance().currentLanguageName());
    }

    psvitaalive::ui::FullCatalogScreen screen;screen.setImageCache(&images);
    screen.setCatalogChangeCallback([&](psvitaalive::ui::CatalogType next){psvitaalive::diagnostics::log(std::string("[UI] catalog requested: ")+psvitaalive::ui::catalogName(next));images.cancelQueuedRequests();return catalogs.request(next);});
    screen.setSearchCallback([&](const std::string& current){
        psvitaalive::ime_diagnostics::setSession(gIme.nextSessionId());
        psvitaalive::ime_diagnostics::record("search_request", gIme.nextSessionId(), (int)current.size(), 0, true);
        // Free catalog image VRAM and cancel image downloads BEFORE opening
        // CommonDialog. Its input/UTF buffers and all UI state remain intact.
        screen.prepareForIme();
        if (!gIme.open(current, "Search catalog")) {
            psvitaalive::ime_diagnostics::record("open_rejected", psvitaalive::ime_diagnostics::activeSession(), 0, 0, true);
            psvitaalive::diagnostics::log("[UI] search IME request rejected");
            screen.showToast("Keyboard unavailable", 1800);
        } else {
            // The callback is inside handleInput/handleTouch; draw the lightweight
            // CommonDialog frame immediately, not one heavy frame later.
            screen.setExternalInputBlocked(true);
        }
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
                if(!promptZipDestination(zipDestination)){psvitaalive::diagnostics::log("[UI] ZIP destination cancelled");return false;}
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
                if(!promptZipDestination(zipDestination)){
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
while(true){
        screen.setExternalInputBlocked(gIme.busy());
        if(!screen.updateAndDraw()) break;

        // CommonDialog was serviced during the frame. Poll IME only afterwards
        // so FINISHED -> GetResult -> Term follows the VitaSDK ordering.
        gIme.poll();
        std::string imeText;
        bool imeAccepted = false;
        if(gIme.takeFinished(imeText, imeAccepted)){
            psvitaalive::ime_diagnostics::record("search_result_ready", psvitaalive::ime_diagnostics::activeSession(), imeAccepted ? 1 : 0, (int)imeText.size(), true);
            if(imeAccepted){
                psvitaalive::ime_diagnostics::record("search_apply_before", psvitaalive::ime_diagnostics::activeSession(), (int)imeText.size());
                screen.completeSearch(imeText);
                psvitaalive::ime_diagnostics::record("search_apply_after", psvitaalive::ime_diagnostics::activeSession(), (int)imeText.size(), 0, true);
            }
        }
        // After a few frames, surface plugin warnings safely.
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
                    const auto st = catalogs.status();
                    if(st.message == "Catalog updated")
                        screen.showToast("Catalog updated", 1800);
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

const psvitaalive::InstallStatus cur=installer.status();using InstallState=psvitaalive::InstallStatus::State;
        // One refresh on Downloading -> Installing (download finished), and
        // one on any terminal install outcome (success/error/cancel/cleanup).
        // State-edge detection prevents refreshing on every UI frame.
        if (cur.state != previousStorageInstallState) {
            const bool downloaded = previousStorageInstallState == InstallState::Downloading
                                    && cur.state == InstallState::Installing;
            const bool finished = cur.state == InstallState::Completed
                               || cur.state == InstallState::Failed
                               || cur.state == InstallState::Cancelled;
            if (downloaded || finished) screen.requestUx0SpaceRefresh();
            previousStorageInstallState = cur.state;
        }
        images.setNetworkPaused(gIme.busy()||cur.state==InstallState::Downloading||cur.state==InstallState::Installing);const bool active=cur.state==InstallState::Downloading||cur.state==InstallState::Installing||cur.state==InstallState::Completed||cur.state==InstallState::Failed||cur.state==InstallState::Cancelled;int outcome=0;if(cur.state==InstallState::Completed)outcome=1;else if(cur.state==InstallState::Cancelled)outcome=3;else if(cur.state==InstallState::Failed)outcome=2;screen.setInstallProgress(active,cur.current,cur.total,cur.bytesPerSecond,cur.stage,cur.fileName,cur.message,outcome,cur.liveAreaOk,cur.installPath,cur.titleId,cur.resultAutoCloseRemainingMs,cur.needsReboot,cur.zipRecoveryAvailable,cur.zipRecoveryPath,cur.zipRecoveryExtractPath,cur.zipRecoveryMayBeCorrupt);
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
    }

    screen.setInstallProgress(false,0,0,0,"","","",0,false,"","");gIme.shutdown();screen.shutdown();installer.shutdown();catalogs.shutdown();images.shutdown();sceAppUtilShutdown();psvitaalive::ime_diagnostics::shutdown();psvitaalive::diagnostics::log("PSVitaAlive session END");psvitaalive::diagnostics::shutdown();sceKernelExitProcess(0);return 0;
}
