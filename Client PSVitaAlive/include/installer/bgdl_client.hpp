#pragma once

#include <cstdint>
#include <string>

namespace psvitaalive {

enum class BgdlTaskType : int {
    Image = 0x01,
    Audio = 0x02,
    Video = 0x03,
    Game = 0x06,       // Vita game PKG
    AddCont = 0x07,    // DLC
    GameUpdate = 0x08,
    Theme = 0x0C,
    Psp = 0x00
};

struct BgdlEnqueueResult {
    bool ok = false;
    uint32_t bgdlId = 0;
    int errorCode = 0;
    std::string message;
};

/**
 * Native Vita background download (ShellSvc / IPMI) — based on FAPS bgdl_poc.
 * Requires taiHEN + SceShellSvc exports. Safe to call: returns ok=false if unavailable.
 *
 * When successful, the system download manager owns the job; the app can exit to LiveArea.
 */
class BgdlClient {
public:
    static BgdlClient& instance();

    /** Load ShellSvc exports once. False on Vita3K / missing taiHEN / resolve fail. */
    bool init();
    bool available() const { return ready_; }

    /**
     * Queue a background download.
     * @param title  Display name in the system downloader (UTF-8).
     * @param url    HTTP(S) PKG/content URL.
     * @param rifPath Optional local path to a .rif (may be empty string).
     * @param type   Content type for the download manager.
     */
    BgdlEnqueueResult enqueue(
        const std::string& title,
        const std::string& url,
        const std::string& rifPath,
        BgdlTaskType type = BgdlTaskType::Game
    );

    /** Heuristic: prefer BGDL for .pkg URLs when method is Auto/Bgdl. */
    static bool looksLikePkgUrl(const std::string& url, const std::string& fileName);

private:
    BgdlClient() = default;
    bool ready_ = false;
    bool initAttempted_ = false;

    // Opaque shell download class state (heap buffers live for process lifetime once ready).
    void* downloadClass_ = nullptr;
};

} // namespace psvitaalive
