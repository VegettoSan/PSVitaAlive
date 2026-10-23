#include "network/archive_service_probe.hpp"
#include "network/http_client.hpp"
#include "diagnostic_logger.hpp"

#include <psp2/kernel/threadmgr.h>
#include <psp2/net/netctl.h>
#include <cstring>
#include <string>

namespace psvitaalive {
ArchiveServiceProbe::~ArchiveServiceProbe() {
    if (thread_ >= 0) {
        sceKernelWaitThreadEnd(thread_, nullptr, nullptr);
        sceKernelDeleteThread(thread_);
    }
}
bool ArchiveServiceProbe::start() {
    if (thread_ >= 0) return false;
    thread_ = sceKernelCreateThread("PSVAArchiveProbe", &ArchiveServiceProbe::entry,
                                   0x10000100, 128 * 1024, 0, 0, nullptr);
    if (thread_ < 0) {
        result_.store(static_cast<int>(Result::Unknown), std::memory_order_release);
        return false;
    }
    ArchiveServiceProbe* self = this;
    if (sceKernelStartThread(thread_, sizeof(self), &self) < 0) {
        sceKernelDeleteThread(thread_);
        thread_ = -1;
        result_.store(static_cast<int>(Result::Unknown), std::memory_order_release);
        return false;
    }
    return true;
}
int ArchiveServiceProbe::entry(SceSize, void* arg) {
    ArchiveServiceProbe* self = nullptr;
    if (arg) std::memcpy(&self, arg, sizeof(self));
    return self ? self->run() : -1;
}
int ArchiveServiceProbe::run() {
    int netState = 0;
    if (sceNetCtlInetGetState(&netState) >= 0 &&
        netState != SCE_NETCTL_STATE_CONNECTED) {
        diagnostics::log("[ArchiveProbe] Wi-Fi disconnected; warning skipped");
        result_.store(static_cast<int>(Result::Unknown), std::memory_order_release);
        return 0;
    }
    HttpClient http;
    if (http.init() != HttpResult::Ok) {
        diagnostics::log("[ArchiveProbe] HTTP unavailable; warning skipped");
        result_.store(static_cast<int>(Result::Unknown), std::memory_order_release);
        return 0;
    }
    std::string etag, modified;
    const HttpResult rc = http.fetchRemoteValidators("https://archive.org/", etag, modified);
    const int code = http.lastStatusCode();
    // HTTP 4xx means the server answered; it is not a network outage.
    const bool answered = (rc == HttpResult::Ok || rc == HttpResult::HttpError) &&
                          code >= 200 && code < 500;
    const bool failed = !answered &&
        (rc == HttpResult::NetworkError || rc == HttpResult::SslError ||
         (code >= 500 && code <= 599));
    diagnostics::log(std::string("[ArchiveProbe] status=") + std::to_string(code) +
                     " result=" + toString(rc) + " unreachable=" + (failed ? "1" : "0"));
    result_.store(static_cast<int>(answered ? Result::Available :
                  failed ? Result::Unreachable : Result::Unknown),
                  std::memory_order_release);
    http.shutdown();
    return 0;
}
} // namespace psvitaalive
