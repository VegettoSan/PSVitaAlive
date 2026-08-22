#include "installer/bgdl_client.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <taihen.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace psvitaalive {
namespace {

// --- Structures mirrored from FAPS bgdl_poc (GPL-2.0) / PKGj lineage ---

struct ipmi_download_param {
    int type[2];
    char unk_0x08[0x68];
    char url[0x800];
    char icon_path[0x100];
    char title[0x33A];
    char license_path[0x100];
    char unk_0xDAA[0x16];
};

struct sce_ipmi_download_param {
    union {
        struct {
            uint32_t* ptr_to_dc0_ptr;
            uint32_t* ptr_to_2e0_ptr;
            uint32_t unk_1;
            uint32_t unk_2;
            uint32_t unk_3;
            ipmi_download_param* addr_DC0;
            uint32_t sizeDC0;
        } init;
        struct {
            uint32_t* result;
            uint32_t unk_2;
            uint32_t unk_3;
            uint32_t unk_4;
            uint32_t unk_5;
            uint32_t unk_6;
            uint32_t unk_7;
        } state;
    };
    void* addr_2E0;
    uint32_t size2E0;
    uint32_t unk_4;
    uint32_t* pBgdlId;
    uint32_t unk_5;
    uint32_t* result;
    uint32_t unk_4_2;
    uint32_t shell_func_8;
};

struct shellsvc_init_struct {
    uint32_t unk_0;
    char name[0x10];
    void* unk_ptr;
    uint32_t unk_1;
    uint32_t size1;
    uint32_t size2;
    uint32_t unk_2;
    uint32_t unk_3;
    uint32_t unk_4;
    uint32_t unk_5;
    char padding[0x84];
    uint32_t unk_7;
    uint32_t unk_8;
    void* unk_ptr_2;
    char padding2[0x88];
};

struct scedownload_class_header {
    uint32_t unk0;
    uint32_t unk1;
    uint32_t unk2;
    uint32_t** func_table;
    uint32_t unk3;
    uint32_t* bufC4;
    uint32_t* buf10000;
};

using SceDownloadInit = int (*)(uint32_t** ipmi_sce_download_ptr, void* ipmi_sce_download_ptr_deref, int unk_1, shellsvc_init_struct* bufc8, int unk_2);
using SceDownloadChangeState = int (*)(uint32_t** ipmi_sce_download_ptr, int cmd, void* ptr_to_dc0_ptr, int unk_1, sce_ipmi_download_param r5);

struct scedownload_class {
    shellsvc_init_struct init_header;
    scedownload_class_header* class_header;
    SceDownloadInit init;
    SceDownloadChangeState change_state;
};

int (*SceIpmi_4E255C31)(const char* name, int unk) = nullptr;
int (*SceIpmi_B282B430)(uint32_t*** func_table, const char* name, scedownload_class_header* class_header, uint32_t* buf10000) = nullptr;

bool ensurePlaceholderRif(const std::string& path) {
    // BGDL requires a license path that exists (or empty). Create a tiny placeholder if needed.
    if (path.empty()) return true;
    SceUID fd = sceIoOpen(path.c_str(), SCE_O_RDONLY, 0);
    if (fd >= 0) {
        sceIoClose(fd);
        return true;
    }
    // Ensure parent dir ux0:bgdl exists
    sceIoMkdir("ux0:bgdl", 6);
    fd = sceIoOpen(path.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return false;
    const char zero = 0;
    sceIoWrite(fd, &zero, 1);
    sceIoClose(fd);
    return true;
}

int init_download_class(scedownload_class* cls) {
    std::memset(cls, 0, sizeof(*cls));
    std::strncpy(cls->init_header.name, "SceDownload", 0x10);
    cls->init_header.unk_1 = 1;
    cls->init_header.size1 = 0x1E00;
    cls->init_header.size2 = 0x1E00;
    cls->init_header.unk_2 = 1;
    cls->init_header.unk_3 = 0x0F00;
    cls->init_header.unk_4 = 0x0F00;
    cls->init_header.unk_5 = 1;
    cls->init_header.unk_7 = 2;
    cls->init_header.unk_8 = static_cast<uint32_t>(-1);

    int res = SceIpmi_4E255C31(cls->init_header.name, 0x1E00);
    if (res != 0xc4) return res;

    cls->class_header = static_cast<scedownload_class_header*>(std::calloc(1, 0x18));
    if (!cls->class_header) return -1;
    cls->class_header->bufC4 = static_cast<uint32_t*>(std::calloc(1, static_cast<size_t>(res)));
    if (!cls->class_header->bufC4) {
        std::free(cls->class_header);
        return -1;
    }
    cls->class_header->buf10000 = static_cast<uint32_t*>(std::calloc(1, 0x1000));
    if (!cls->class_header->buf10000) {
        std::free(cls->class_header->bufC4);
        std::free(cls->class_header);
        return -1;
    }

    res = SceIpmi_B282B430(
        &cls->class_header->func_table,
        cls->init_header.name,
        cls->class_header,
        cls->class_header->buf10000
    );
    if (res < 0 || reinterpret_cast<intptr_t>(cls->class_header->func_table) < 0) {
        std::free(cls->class_header->bufC4);
        std::free(cls->class_header->buf10000);
        std::free(cls->class_header);
        return res < 0 ? res : -2;
    }

    cls->init = reinterpret_cast<SceDownloadInit>((*cls->class_header->func_table)[1]);
    cls->change_state = reinterpret_cast<SceDownloadChangeState>((*cls->class_header->func_table)[5]);
    res = cls->init(cls->class_header->func_table, *cls->class_header->func_table, 0x14, &cls->init_header, 2);
    return res;
}

} // namespace

BgdlClient& BgdlClient::instance() {
    static BgdlClient inst;
    return inst;
}

bool BgdlClient::init() {
    if (initAttempted_) return ready_;
    initAttempted_ = true;

    // PKGj path: load libshellsvc then resolve SceShellSvc IPMI exports via taiHEN.
    // Requires eboot built with UNSAFE (see CMakeLists) — same as PKGj.
    static const char* kShellPaths[] = {
        "vs0:sys/external/libshellsvc.suprx",
        "vs0:/sys/external/libshellsvc.suprx",
    };
    SceUID loadUid = -1;
    for (const char* path : kShellPaths) {
        loadUid = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, nullptr);
        diagnostics::log(std::string("[BGDL] LoadStartModule ") + path + " -> " + std::to_string(loadUid));
        if (loadUid >= 0) break;
        // Already loaded often returns an error code; continue to export resolve anyway.
    }

    int r1 = taiGetModuleExportFunc(
        "SceShellSvc", 0xF4E34EDB, 0x4E255C31, reinterpret_cast<uintptr_t*>(&SceIpmi_4E255C31));
    int r2 = taiGetModuleExportFunc(
        "SceShellSvc", 0xF4E34EDB, 0xB282B430, reinterpret_cast<uintptr_t*>(&SceIpmi_B282B430));

    if (r1 < 0 || r2 < 0 || !SceIpmi_4E255C31 || !SceIpmi_B282B430) {
        char hex1[16], hex2[16];
        sceClibSnprintf(hex1, sizeof(hex1), "0x%08X", static_cast<unsigned>(r1));
        sceClibSnprintf(hex2, sizeof(hex2), "0x%08X", static_cast<unsigned>(r2));
        diagnostics::log(std::string("[BGDL] ShellSvc exports unavailable r1=") + hex1 +
                         " r2=" + hex2 + " loadUid=" + std::to_string(loadUid) +
                         " (need UNSAFE eboot + taiHEN; same NIDs as PKGj)");
        ready_ = false;
        return false;
    }

    auto* cls = static_cast<scedownload_class*>(std::calloc(1, sizeof(scedownload_class)));
    if (!cls) {
        ready_ = false;
        return false;
    }
    const int res = init_download_class(cls);
    if (res < 0) {
        diagnostics::log(std::string("[BGDL] init_download_class failed: 0x") + std::to_string(res));
        std::free(cls);
        ready_ = false;
        return false;
    }
    downloadClass_ = cls;
    ready_ = true;
    diagnostics::log("[BGDL] ready (ShellSvc IPMI download class OK)");
    return true;
}

bool BgdlClient::looksLikePkgUrl(const std::string& url, const std::string& fileName) {
    auto lowerEnds = [](const std::string& s, const char* ext) {
        if (s.size() < 4) return false;
        const size_t n = std::strlen(ext);
        if (s.size() < n) return false;
        for (size_t i = 0; i < n; ++i) {
            char c = s[s.size() - n + i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            if (c != ext[i]) return false;
        }
        return true;
    };
    return lowerEnds(fileName, ".pkg") || lowerEnds(url, ".pkg") ||
           (url.find(".pkg") != std::string::npos);
}

BgdlEnqueueResult BgdlClient::enqueue(
    const std::string& title,
    const std::string& url,
    const std::string& rifPath,
    BgdlTaskType type
) {
    BgdlEnqueueResult out;
    if (!init() || !downloadClass_) {
        out.message = "BGDL unavailable (ShellSvc/taiHEN)";
        out.errorCode = -1;
        return out;
    }
    if (url.empty()) {
        out.message = "Empty URL";
        out.errorCode = -2;
        return out;
    }

    auto* cls = static_cast<scedownload_class*>(downloadClass_);
    uint32_t bgdlid = 1;
    uint32_t result = 1;

    sce_ipmi_download_param params{};
    std::memset(&params, 0, sizeof(params));

    params.init.ptr_to_dc0_ptr = reinterpret_cast<uint32_t*>(&params.init.addr_DC0);
    params.init.ptr_to_2e0_ptr = reinterpret_cast<uint32_t*>(&params.addr_2E0);
    params.init.unk_1 = 2;
    params.init.unk_2 = static_cast<uint32_t>(-1);
    params.init.unk_3 = 0;
    params.init.sizeDC0 = 0xDC0;
    params.init.addr_DC0 = static_cast<ipmi_download_param*>(std::calloc(1, params.init.sizeDC0));
    if (!params.init.addr_DC0) {
        out.message = "OOM DC0";
        out.errorCode = -3;
        return out;
    }
    params.size2E0 = 0x2E0;
    params.addr_2E0 = std::calloc(1, params.size2E0);
    if (!params.addr_2E0) {
        std::free(params.init.addr_DC0);
        out.message = "OOM 2E0";
        out.errorCode = -3;
        return out;
    }

    params.pBgdlId = &bgdlid;
    params.unk_5 = 4;
    params.result = &result;
    params.shell_func_8 = (*cls->class_header->func_table)[8];

    std::strncpy(params.init.addr_DC0->url, url.c_str(), sizeof(params.init.addr_DC0->url) - 1);
    std::string rif = rifPath;
    if (rif.empty()) rif = "ux0:bgdl/psvitaalive_rif.dat";
    ensurePlaceholderRif(rif);
    std::strncpy(params.init.addr_DC0->license_path, rif.c_str(), sizeof(params.init.addr_DC0->license_path) - 1);
    std::strncpy(params.init.addr_DC0->title, title.empty() ? "PSVitaAlive" : title.c_str(),
                 sizeof(params.init.addr_DC0->title) - 1);
    // optional icon — leave empty if missing
    std::strncpy(params.init.addr_DC0->icon_path, "ux0:bgdl/icon0.png", sizeof(params.init.addr_DC0->icon_path) - 1);

    const int t = static_cast<int>(type);
    params.init.addr_DC0->type[0] = params.init.addr_DC0->type[1] = t;

    int res = cls->change_state(cls->class_header->func_table, 0x12340012, params.init.ptr_to_dc0_ptr, 1, params);
    if (res < 0) {
        std::free(params.init.addr_DC0);
        std::free(params.addr_2E0);
        out.errorCode = res;
        out.message = "BGDL change_state start failed";
        diagnostics::log(std::string("[BGDL] start failed 0x") + std::to_string(res));
        return out;
    }

    result = 0;
    std::memset(&params, 0, sizeof(params));
    params.state.result = &result;
    params.state.unk_4 = 1;
    params.state.unk_7 = 0x00000A0A;
    res = cls->change_state(cls->class_header->func_table, 0x12340007, 0, 0, params);

    out.ok = (result == 0) || (res >= 0);
    out.bgdlId = bgdlid;
    out.errorCode = res;
    out.message = out.ok ? "Queued in system download manager" : "BGDL commit failed";
    diagnostics::log(std::string("[BGDL] enqueue ok=") + (out.ok ? "1" : "0") +
                     " id=" + std::to_string(bgdlid) + " res=" + std::to_string(res));
    // Note: DC0/2E0 buffers intentionally kept by system after successful queue in PoC;
    // free on failure only. On success the shell may still reference them — leak is intentional
    // and matches upstream PoC lifetime for a single enqueue session.
    if (!out.ok) {
        // params was zeroed; cannot free original DC0 here safely — accepted PoC limitation
    }
    return out;
}

} // namespace psvitaalive
