#include "installer/bgdl_client.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

#include <taihen.h>

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace psvitaalive {
namespace {

// Structures / flow mirrored from PKGj src/bgdl.cpp (RE by @dots_tb et al.).
// Aggressive logging so real-hardware failures are diagnosable in session.log.

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
            uint32_t unk_1; // 2
            uint32_t unk_2; // -1
            uint32_t unk_3; // 0
            ipmi_download_param* addr_DC0;
            uint32_t sizeDC0;
        } init;
        struct {
            int32_t* result;
            uint32_t unk_2;
            uint32_t unk_3;
            uint32_t unk_4; // 1
            uint32_t unk_5;
            uint32_t unk_6;
            uint32_t unk_7; // 0x00000A0A
        } state;
    };
    void* addr_2E0;
    uint32_t size2E0;
    uint32_t unk_4;
    uint32_t* pBgdlId;
    uint32_t unk_5; // 4
    int32_t* result;
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

using SceDownloadInit = int (*)(uint32_t** ipmi_sce_download_ptr, void* ipmi_sce_download_ptr_deref,
                                int unk_1, shellsvc_init_struct* bufc8, int unk_2);
using SceDownloadChangeState = int (*)(uint32_t** ipmi_sce_download_ptr, int cmd, void* ptr_to_dc0_ptr,
                                       int unk_1, sce_ipmi_download_param r5);

struct scedownload_class {
    shellsvc_init_struct init_header;
    scedownload_class_header* class_header;
    SceDownloadInit init;
    SceDownloadChangeState change_state;
};

// Same NIDs as PKGj
static int (*SceIpmi_4E255C31)(const char* name, int unk) = nullptr;
static int (*SceIpmi_B282B430)(uint32_t*** func_table, const char* name,
                               scedownload_class_header* class_header, uint32_t* buf10000) = nullptr;

static void logHex(const char* tag, int code) {
    char buf[96];
    sceClibSnprintf(buf, sizeof(buf), "%s 0x%08X (%d)", tag, (unsigned)code, code);
    diagnostics::log(std::string("[BGDL] ") + buf);
    sceClibPrintf("[BGDL] %s\n", buf);
}

static int countPendingBgdl() {
    // PKGj refuses when ux0:bgdl/t has >= 32 entries.
    SceUID dfd = sceIoDopen("ux0:bgdl/t");
    if (dfd < 0) return 0;
    int n = 0;
    SceIoDirent de;
    while (sceIoDread(dfd, &de) > 0) {
        if (de.d_name[0] == '.') continue;
        ++n;
    }
    sceIoDclose(dfd);
    return n;
}

static int init_download_class(scedownload_class* cls) {
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

    diagnostics::log("[BGDL] init_download_class: calling SceIpmi_4E255C31(SceDownload, 0x1E00)");
    int res = SceIpmi_4E255C31(cls->init_header.name, 0x1E00);
    logHex("SceIpmi_4E255C31", res);
    if (res != 0xc4) {
        diagnostics::log("[BGDL] expected 0xC4 from 4E255C31 — abort class init");
        return res != 0 ? res : -1;
    }

    cls->class_header = reinterpret_cast<scedownload_class_header*>(new char[0x18]());
    cls->class_header->bufC4 = reinterpret_cast<uint32_t*>(new char[static_cast<size_t>(res)]());
    cls->class_header->buf10000 = reinterpret_cast<uint32_t*>(new char[0x1000]());

    diagnostics::log("[BGDL] init_download_class: calling SceIpmi_B282B430");
    res = SceIpmi_B282B430(
        &cls->class_header->func_table,
        cls->init_header.name,
        cls->class_header,
        cls->class_header->buf10000);
    logHex("SceIpmi_B282B430", res);
    if (res != 0) {
        delete[] reinterpret_cast<char*>(cls->class_header->bufC4);
        delete[] reinterpret_cast<char*>(cls->class_header->buf10000);
        delete[] reinterpret_cast<char*>(cls->class_header);
        cls->class_header = nullptr;
        return res;
    }

    if (!cls->class_header->func_table || !*cls->class_header->func_table) {
        diagnostics::log("[BGDL] func_table null after B282B430");
        return -2;
    }

    cls->init = reinterpret_cast<SceDownloadInit>((*cls->class_header->func_table)[1]);
    cls->change_state = reinterpret_cast<SceDownloadChangeState>((*cls->class_header->func_table)[5]);
    diagnostics::log(std::string("[BGDL] vtable init=") +
                     std::to_string(reinterpret_cast<uintptr_t>(cls->init)) +
                     " change_state=" +
                     std::to_string(reinterpret_cast<uintptr_t>(cls->change_state)));

    res = cls->init(
        cls->class_header->func_table,
        *cls->class_header->func_table,
        0x14,
        &cls->init_header,
        2);
    logHex("SceDownload.init", res);
    if (res != 0) {
        delete[] reinterpret_cast<char*>(cls->class_header->bufC4);
        delete[] reinterpret_cast<char*>(cls->class_header->buf10000);
        delete[] reinterpret_cast<char*>(cls->class_header);
        cls->class_header = nullptr;
        return res;
    }
    diagnostics::log("[BGDL] init_download_class OK");
    return 0;
}

static bool resolveShellExports() {
    SceIpmi_4E255C31 = nullptr;
    SceIpmi_B282B430 = nullptr;

    // Module info (diagnostic)
    tai_module_info_t info{};
    info.size = sizeof(info);
    const int mi = taiGetModuleInfo("SceShellSvc", &info);
    logHex("taiGetModuleInfo(SceShellSvc)", mi);
    if (mi >= 0) {
        char mbuf[128];
        sceClibSnprintf(mbuf, sizeof(mbuf),
                        "[BGDL] SceShellSvc modid=%d nid=%08X",
                        (int)info.modid, (unsigned)info.module_nid);
        diagnostics::log(mbuf);
    }

    static const char* kPaths[] = {
        "vs0:sys/external/libshellsvc.suprx",
        "vs0:/sys/external/libshellsvc.suprx",
    };
    for (const char* path : kPaths) {
        const SceUID uid = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, nullptr);
        char lbuf[160];
        sceClibSnprintf(lbuf, sizeof(lbuf), "[BGDL] LoadStartModule %s -> %d (0x%08X)",
                        path, (int)uid, (unsigned)uid);
        diagnostics::log(lbuf);
        sceClibPrintf("%s\n", lbuf);
        if (uid >= 0) break;
    }

    // Retry module info after load
    info = {};
    info.size = sizeof(info);
    const int mi2 = taiGetModuleInfo("SceShellSvc", &info);
    logHex("taiGetModuleInfo(after load)", mi2);

    const int r1 = taiGetModuleExportFunc(
        "SceShellSvc", 0xF4E34EDB, 0x4E255C31,
        reinterpret_cast<uintptr_t*>(&SceIpmi_4E255C31));
    const int r2 = taiGetModuleExportFunc(
        "SceShellSvc", 0xF4E34EDB, 0xB282B430,
        reinterpret_cast<uintptr_t*>(&SceIpmi_B282B430));
    logHex("taiGetExport 4E255C31", r1);
    logHex("taiGetExport B282B430", r2);

    char pbuf[128];
    sceClibSnprintf(pbuf, sizeof(pbuf),
                    "[BGDL] export ptrs 4E255C31=%p B282B430=%p",
                    (void*)SceIpmi_4E255C31, (void*)SceIpmi_B282B430);
    diagnostics::log(pbuf);
    sceClibPrintf("%s\n", pbuf);

    if (r1 < 0 || r2 < 0 || !SceIpmi_4E255C31 || !SceIpmi_B282B430) {
        diagnostics::log("[BGDL] ShellSvc IPMI exports unresolved — need UNSAFE eboot + working taiHEN (same as PKGj)");
        return false;
    }
    return true;
}

} // namespace

BgdlClient& BgdlClient::instance() {
    static BgdlClient inst;
    return inst;
}

bool BgdlClient::init() {
    if (ready_ && downloadClass_) return true;

    diagnostics::log("[BGDL] ===== init begin =====");
    // Allow retry after previous failure (do not permanent-lock).
    initAttempted_ = true;

    if (!resolveShellExports()) {
        ready_ = false;
        downloadClass_ = nullptr;
        diagnostics::log("[BGDL] ===== init FAILED (exports) =====");
        return false;
    }

    auto* cls = new scedownload_class();
    const int ir = init_download_class(cls);
    if (ir != 0) {
        logHex("init_download_class failed", ir);
        delete cls;
        ready_ = false;
        downloadClass_ = nullptr;
        diagnostics::log("[BGDL] ===== init FAILED (class) =====");
        return false;
    }

    downloadClass_ = cls;
    ready_ = true;
    diagnostics::log("[BGDL] ===== init OK (PKGj-aligned SceDownload class) =====");
    return true;
}

bool BgdlClient::looksLikePkgUrl(const std::string& url, const std::string& fileName) {
    auto endsPkg = [](const std::string& s) {
        if (s.size() < 4) return false;
        const char* e = s.c_str() + (s.size() - 4);
        return (e[0] == '.' && (e[1] == 'p' || e[1] == 'P') &&
                (e[2] == 'k' || e[2] == 'K') && (e[3] == 'g' || e[3] == 'G'));
    };
    if (endsPkg(fileName) || endsPkg(url)) return true;
    return url.find(".pkg") != std::string::npos || url.find(".PKG") != std::string::npos;
}

BgdlEnqueueResult BgdlClient::enqueue(
    const std::string& title,
    const std::string& url,
    const std::string& rifPath,
    BgdlTaskType type
) {
    BgdlEnqueueResult out;
    diagnostics::log(std::string("[BGDL] enqueue title=") + title +
                     " type=" + std::to_string(static_cast<int>(type)) +
                     " url=" + url.substr(0, 96) +
                     " rif=" + rifPath);

    if (!init() || !downloadClass_) {
        out.message = "BGDL unavailable (ShellSvc/taiHEN)";
        out.errorCode = -1;
        diagnostics::log(std::string("[BGDL] enqueue abort: ") + out.message);
        return out;
    }
    if (url.empty()) {
        out.message = "Empty URL";
        out.errorCode = -2;
        return out;
    }

    const int pending = countPendingBgdl();
    diagnostics::log(std::string("[BGDL] pending jobs in ux0:bgdl/t = ") + std::to_string(pending));
    if (pending >= 32) {
        out.message = "Too many pending BGDL jobs (clear LiveArea notifications)";
        out.errorCode = -4;
        diagnostics::log(std::string("[BGDL] ") + out.message);
        return out;
    }

    sceIoMkdir("ux0:bgdl", 0777);

    auto* cls = static_cast<scedownload_class*>(downloadClass_);

    // Match PKGj: stack vectors for DC0 / 2E0, freed after successful queue.
    int32_t result = 0;
    int32_t bgdlid = 1;

    sce_ipmi_download_param params;
    std::memset(&params, 0, sizeof(params));

    params.init.ptr_to_dc0_ptr = reinterpret_cast<uint32_t*>(&params.init.addr_DC0);
    params.init.ptr_to_2e0_ptr = reinterpret_cast<uint32_t*>(&params.addr_2E0);
    params.init.unk_1 = 2;
    params.init.unk_2 = static_cast<uint32_t>(-1);
    params.init.unk_3 = 0;
    params.init.sizeDC0 = 0xDC0;

    std::vector<uint8_t> buf_dc0(params.init.sizeDC0, 0);
    params.init.addr_DC0 = reinterpret_cast<ipmi_download_param*>(buf_dc0.data());

    params.size2E0 = 0x2E0;
    std::vector<uint8_t> buf_2e0(params.size2E0, 0);
    params.addr_2E0 = buf_2e0.data();

    params.pBgdlId = reinterpret_cast<uint32_t*>(&bgdlid);
    params.unk_5 = 4;
    params.result = &result;
    params.shell_func_8 = (*cls->class_header->func_table)[8];

    std::strncpy(params.init.addr_DC0->url, url.c_str(), sizeof(params.init.addr_DC0->url) - 1);
    if (!rifPath.empty()) {
        std::strncpy(params.init.addr_DC0->license_path, rifPath.c_str(),
                     sizeof(params.init.addr_DC0->license_path) - 1);
    } else {
        params.init.addr_DC0->license_path[0] = '\0';
    }
    std::strncpy(params.init.addr_DC0->title,
                 title.empty() ? "PSVitaAlive" : title.c_str(),
                 sizeof(params.init.addr_DC0->title) - 1);
    // PKGj always sets this path string (file need not exist for queue).
    std::strncpy(params.init.addr_DC0->icon_path, "ux0:bgdl/icon0.png",
                 sizeof(params.init.addr_DC0->icon_path) - 1);

    const int t = static_cast<int>(type);
    params.init.addr_DC0->type[0] = params.init.addr_DC0->type[1] = t;

    diagnostics::log("[BGDL] change_state 0x12340012 (start)");
    int res = cls->change_state(
        cls->class_header->func_table,
        0x12340012,
        params.init.ptr_to_dc0_ptr,
        1,
        params);
    logHex("change_state start res", res);
    diagnostics::log(std::string("[BGDL] after start result=") + std::to_string(result) +
                     " bgdlid=" + std::to_string(bgdlid));

    if (res < 0 || result < 0 || bgdlid < 0) {
        out.ok = false;
        out.errorCode = res < 0 ? res : static_cast<int>(result);
        out.message = "BGDL start rejected";
        diagnostics::log(std::string("[BGDL] start rejected res=") + std::to_string(res) +
                         " result=" + std::to_string(result) +
                         " bgdlid=" + std::to_string(bgdlid));
        return out;
    }

    result = 0;
    std::memset(&params, 0, sizeof(params));
    params.state.result = &result;
    params.state.unk_4 = 1;
    params.state.unk_7 = 0x00000A0A;

    diagnostics::log("[BGDL] change_state 0x12340007 (commit)");
    res = cls->change_state(cls->class_header->func_table, 0x12340007, 0, 0, params);
    logHex("change_state commit res", res);
    diagnostics::log(std::string("[BGDL] after commit result=") + std::to_string(result));

    if (res < 0 || result < 0) {
        out.ok = false;
        out.errorCode = res < 0 ? res : static_cast<int>(result);
        out.message = "BGDL commit failed";
        diagnostics::log(std::string("[BGDL] commit failed res=") + std::to_string(res) +
                         " result=" + std::to_string(result));
        return out;
    }

    // PKGj clears buffers after successful queue.
    buf_dc0.clear();
    buf_2e0.clear();

    out.ok = true;
    out.bgdlId = static_cast<uint32_t>(bgdlid);
    out.errorCode = 0;
    out.message = "Queued in system download manager";
    diagnostics::log(std::string("[BGDL] enqueue OK id=") + std::to_string(bgdlid) +
                     " — check LiveArea notifications");
    return out;
}

} // namespace psvitaalive
