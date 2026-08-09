#include "network/http_client.hpp"

#include <psp2/kernel/clib.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/net/http.h>
#include <psp2/libssl.h>
#include <psp2/io/fcntl.h>
#include <psp2/sysmodule.h>

#include <cstring>
#include <vector>

namespace psvitaalive {

namespace {
constexpr int NET_MEM_SIZE = 512 * 1024;
constexpr size_t DOWNLOAD_CHUNK = 64 * 1024;
constexpr int HTTP_TIMEOUT_US = 30 * 1000 * 1000;
} // namespace

const char* toString(HttpResult r) {
    switch (r) {
        case HttpResult::Ok: return "Ok";
        case HttpResult::NotInitialized: return "NotInitialized";
        case HttpResult::NetworkError: return "NetworkError";
        case HttpResult::HttpError: return "HttpError";
        case HttpResult::SslError: return "SslError";
        case HttpResult::IoError: return "IoError";
        case HttpResult::Cancelled: return "Cancelled";
        case HttpResult::InvalidArgument: return "InvalidArgument";
        default: return "Unknown";
    }
}

HttpClient::HttpClient() = default;
HttpClient::~HttpClient() { shutdown(); }

void HttpClient::setError(const std::string& msg) {
    lastError_ = msg;
    sceClibPrintf("[HttpClient] %s\n", msg.c_str());
}

HttpResult HttpClient::init() {
    if (initialized_) return HttpResult::Ok;
    lastError_.clear();
    lastStatus_ = 0;
    lastRangeAccepted_ = false;

    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);

    static char netMemory[NET_MEM_SIZE];
    SceNetInitParam netParam;
    std::memset(&netParam, 0, sizeof(netParam));
    netParam.memory = netMemory;
    netParam.size = sizeof(netMemory);
    netParam.flags = 0;

    int ret = sceNetInit(&netParam);
    if (ret < 0) sceClibPrintf("[HttpClient] sceNetInit: 0x%08X\n", ret);
    ret = sceNetCtlInit();
    if (ret < 0) sceClibPrintf("[HttpClient] sceNetCtlInit: 0x%08X\n", ret);

    ret = sceSslInit(300 * 1024);
    if (ret < 0) {
        char buf[64];
        sceClibSnprintf(buf, sizeof(buf), "sceSslInit failed: 0x%08X", ret);
        setError(buf);
        return HttpResult::SslError;
    }

    ret = sceHttpInit(1024 * 1024);
    if (ret < 0) {
        char buf[64];
        sceClibSnprintf(buf, sizeof(buf), "sceHttpInit failed: 0x%08X", ret);
        setError(buf);
        return HttpResult::HttpError;
    }

    tplSsl_ = sceHttpCreateTemplate("PSVitaAlive/1.0", SCE_HTTP_VERSION_1_1, SCE_TRUE);
    if (tplSsl_ < 0) { setError("template SSL failed"); return HttpResult::HttpError; }
    tplHttp_ = sceHttpCreateTemplate("PSVitaAlive/1.0", SCE_HTTP_VERSION_1_1, SCE_FALSE);
    if (tplHttp_ < 0) { setError("template HTTP failed"); return HttpResult::HttpError; }

    for (int tpl : {tplSsl_, tplHttp_}) {
        sceHttpSetConnectTimeOut(tpl, HTTP_TIMEOUT_US);
        sceHttpSetSendTimeOut(tpl, HTTP_TIMEOUT_US);
        sceHttpSetRecvTimeOut(tpl, HTTP_TIMEOUT_US);
    }

    initialized_ = true;
    sceClibPrintf("[HttpClient] initialized\n");
    return HttpResult::Ok;
}

void HttpClient::shutdown() {
    if (tplHttp_ >= 0) { sceHttpDeleteTemplate(tplHttp_); tplHttp_ = -1; }
    if (tplSsl_ >= 0) { sceHttpDeleteTemplate(tplSsl_); tplSsl_ = -1; }
    if (initialized_) {
        sceHttpTerm();
        sceSslTerm();
        initialized_ = false;
        sceClibPrintf("[HttpClient] shutdown\n");
    }
}

HttpResult HttpClient::downloadToFile(
    const std::string& url,
    const std::string& destinationPath,
    uint64_t resumeOffset,
    HttpProgressFn onProgress,
    HttpCancelFn shouldCancel
) {
    lastStatus_ = 0;
    lastRangeAccepted_ = false;
    lastError_.clear();

    if (!initialized_) { setError("not initialized"); return HttpResult::NotInitialized; }
    if (url.empty() || destinationPath.empty()) {
        setError("empty url or path");
        return HttpResult::InvalidArgument;
    }

    const bool isHttps = (url.rfind("https://", 0) == 0);
    const bool isHttp = (url.rfind("http://", 0) == 0);
    if (!isHttps && !isHttp) {
        setError("url must start with http:// or https://");
        return HttpResult::InvalidArgument;
    }

    int tpl = isHttps ? tplSsl_ : tplHttp_;
    int conn = sceHttpCreateConnectionWithURL(tpl, url.c_str(), SCE_TRUE);
    if (conn < 0) {
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "CreateConnection failed: 0x%08X", conn);
        setError(buf);
        return HttpResult::HttpError;
    }

    int req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url.c_str(), 0);
    if (req < 0) {
        sceHttpDeleteConnection(conn);
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "CreateRequest failed: 0x%08X", req);
        setError(buf);
        return HttpResult::HttpError;
    }

    if (resumeOffset > 0) {
        char rangeHdr[64];
        sceClibSnprintf(rangeHdr, sizeof(rangeHdr), "bytes=%llu-", (unsigned long long)resumeOffset);
        int hr = sceHttpAddRequestHeader(req, "Range", rangeHdr, SCE_HTTP_HEADER_OVERWRITE);
        sceClibPrintf("[HttpClient] Range %s (add=%d)\n", rangeHdr, hr);
    }

    int ret = sceHttpSendRequest(req, nullptr, 0);
    if (ret < 0) {
        sceHttpDeleteRequest(req);
        sceHttpDeleteConnection(conn);
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "SendRequest failed: 0x%08X", ret);
        setError(buf);
        return HttpResult::HttpError;
    }

    int status = 0;
    ret = sceHttpGetStatusCode(req, &status);
    lastStatus_ = status;

    if (resumeOffset > 0 && status == 206) {
        lastRangeAccepted_ = true;
    }

    // If we asked for range but got 200, server ignored Range -> restart from 0
    bool restartFile = (resumeOffset == 0) || (status == 200);
    if (resumeOffset > 0 && status == 200) {
        sceClibPrintf("[HttpClient] server ignored Range, restarting file\n");
        resumeOffset = 0;
        lastRangeAccepted_ = false;
    }

    if (ret < 0 || (status != 200 && status != 206)) {
        sceHttpDeleteRequest(req);
        sceHttpDeleteConnection(conn);
        char buf[80];
        sceClibSnprintf(buf, sizeof(buf), "HTTP status %d (ret=0x%08X)", status, ret);
        setError(buf);
        return HttpResult::HttpError;
    }

    uint64_t contentLength = 0;
    uint64_t resultLength = 0;
    if (sceHttpGetResponseContentLength(req, &resultLength) >= 0) {
        contentLength = resultLength;
    }

    int flags = SCE_O_WRONLY | SCE_O_CREAT;
    flags |= restartFile ? SCE_O_TRUNC : SCE_O_APPEND;

    SceUID fd = sceIoOpen(destinationPath.c_str(), flags, 0777);
    if (fd < 0) {
        sceHttpDeleteRequest(req);
        sceHttpDeleteConnection(conn);
        setError("cannot open destination file");
        return HttpResult::IoError;
    }

    std::vector<char> buffer(DOWNLOAD_CHUNK);
    uint64_t sessionDownloaded = 0;
    HttpResult outcome = HttpResult::Ok;

    while (true) {
        if (shouldCancel && shouldCancel()) {
            setError("cancelled");
            outcome = HttpResult::Cancelled;
            break;
        }

        int n = sceHttpReadData(req, buffer.data(), static_cast<unsigned int>(buffer.size()));
        if (n < 0) {
            char buf[80];
            sceClibSnprintf(buf, sizeof(buf), "ReadData failed: 0x%08X", n);
            setError(buf);
            outcome = HttpResult::HttpError;
            break;
        }
        if (n == 0) break;

        int written = 0;
        while (written < n) {
            int w = sceIoWrite(fd, buffer.data() + written, n - written);
            if (w <= 0) {
                setError("sceIoWrite failed");
                outcome = HttpResult::IoError;
                break;
            }
            written += w;
        }
        if (outcome != HttpResult::Ok) break;

        sessionDownloaded += static_cast<uint64_t>(n);
        if (onProgress) {
            HttpProgress p;
            p.downloaded = sessionDownloaded;
            p.absoluteDownloaded = resumeOffset + sessionDownloaded;
            // When 206, contentLength is remaining; when 200, full size
            if (status == 206 && contentLength > 0) {
                p.total = resumeOffset + contentLength;
            } else {
                p.total = contentLength;
            }
            onProgress(p);
        }
    }

    sceIoClose(fd);
    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);

    if (outcome == HttpResult::Ok) {
        sceClibPrintf("[HttpClient] done session=%llu abs=%llu status=%d range=%d\n",
                      (unsigned long long)sessionDownloaded,
                      (unsigned long long)(resumeOffset + sessionDownloaded),
                      status,
                      lastRangeAccepted_ ? 1 : 0);
    }
    return outcome;
}

} // namespace psvitaalive
