#pragma once

#include <string>
#include <algorithm>

namespace psvitaalive {
namespace ui {

// Presentation-only classification. Never used to decide whether a download,
// ZIP extraction or installation succeeds, retries or deletes saved files.
enum class InstallFailureKind {
    Network,
    HttpMissing,
    HttpDenied,
    HttpBusy,
    HttpServer,
    Tls,
    MediaFire,
    Space,
    Storage,
    ArchiveCorrupt,
    ArchiveUnsafe,
    Unsupported,
    Installer,
    Other
};
inline bool failureHas(const std::string& value, const char* needle) {
    return value.find(needle) != std::string::npos;
}
inline bool failureAny(const std::string& value, const char* a, const char* b = "",
                       const char* c = "", const char* d = "") {
    return (a[0] && failureHas(value,a)) || (b[0] && failureHas(value,b)) ||
           (c[0] && failureHas(value,c)) || (d[0] && failureHas(value,d));
}
inline InstallFailureKind classifyInstallFailure(const std::string& message,
                                                const std::string& stage) {
    std::string m = message;
    // Case insensitive for diagnostic ASCII codes. Non-ASCII localized
    // messages keep their original bytes and can match stable stems.
    for (char& ch : m)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');

    if (stage == "Space" ||
        failureAny(m,"not enough free space","insufficient space","no hay espacio","espacio libre en ux0") ||
        failureAny(m,"disk full","no space left","free space required","not enough space"))
        return InstallFailureKind::Space;
    if (failureAny(m,"unsafe zip entry","unsafe zip staging","unsafe path:", "unsafe zip") ||
        failureAny(m,"refusing overwrite","destination collides","path traversal",
                   "destination now contains files"))
        return InstallFailureKind::ArchiveUnsafe;
    if (failureAny(m,"zip_fread failed","[zipdiag] entry_read_failed","zip_fopen_index failed",
                   "zip error:") ||
        failureAny(m,"archive may be corrupt","zip64","eocd","invalid zip") ||
        failureAny(m,"zip_get_num_entries failed","crc error","invalid compressed data","zlib error"))
        return InstallFailureKind::ArchiveCorrupt;
    // Legacy enum label stays internal; generic link-resolver errors must
    // preserve the same translated user-friendly error category.
    if (failureAny(m,"mediafire","download exceeded expected size",
                   "download link resolution failed")) return InstallFailureKind::MediaFire;
    // HTTP 404/410 are a missing *resource*, not a whole service outage.
    if (failureAny(m,"http status 404","http status 410","http 404","http 410"))
        return InstallFailureKind::HttpMissing;
    if (failureAny(m,"http status 401","http status 403","http 401","http 403") ||
        failureAny(m,"http status 451","http 451"))
        return InstallFailureKind::HttpDenied;
    if (failureAny(m,"http status 429","http 429","http status 408","http 408"))
        return InstallFailureKind::HttpBusy;
    if (failureAny(m,"http status 500","http status 502","http status 503","http status 504") ||
        failureAny(m,"http status 520","http status 521","http status 522","http status 523") ||
        failureAny(m,"http status 524","http status 501","http status 505"))
        return InstallFailureKind::HttpServer;
    if (failureAny(m,"ssl","tls","certificate","curl error 35") ||
        failureAny(m,"curl error 60","curl error 51","curl error 58","curl error 77"))
        return InstallFailureKind::Tls;
    if (failureAny(m,"source not responding","could not resolve host","connection timed out",
                   "failed to connect") ||
        failureAny(m,"curl error 6:","curl error 7:","curl error 28:","curl error 56:") ||
        failureAny(m,"no internet connection","sin conex","sin internet","network error"))
        return InstallFailureKind::Network;
    if (failureAny(m,"unsupported format","invalid vpk layout","not iso/cso","not pbp") ||
        failureAny(m,"game files / data files must be zip","not pkg","unknown format"))
        return InstallFailureKind::Unsupported;
    if (failureAny(m,"sceiowrite","sceioclose","cannot open destination","cannot create directory") ||
        failureAny(m,"mkdir failed","rename part->final","cannot create destination",
                   "cannot open source") ||
        failureAny(m,"failed to write","write failed","read failed","cannot create dir"))
        return InstallFailureKind::Storage;
    if (failureAny(m,"promote failed","promoter","install failed","installation failed") ||
        failureAny(m,"plugin","bgdl","license","sce_sys/param.sfo"))
        return InstallFailureKind::Installer;
    return InstallFailureKind::Other;
}
inline bool shouldReportInstallFailure(InstallFailureKind kind) {
    switch (kind) {
        case InstallFailureKind::Network:
        case InstallFailureKind::HttpMissing:
        case InstallFailureKind::HttpDenied:
        case InstallFailureKind::HttpBusy:
        case InstallFailureKind::HttpServer:
        case InstallFailureKind::Tls:
        case InstallFailureKind::Space:
            return false;
        default: return true;
    }
}
inline const char* installFailureLocalizationSuffix(InstallFailureKind kind) {
    switch (kind) {
        case InstallFailureKind::Network: return "NETWORK";
        case InstallFailureKind::HttpMissing: return "HTTP_MISSING";
        case InstallFailureKind::HttpDenied: return "HTTP_DENIED";
        case InstallFailureKind::HttpBusy: return "HTTP_BUSY";
        case InstallFailureKind::HttpServer: return "HTTP_SERVER";
        case InstallFailureKind::Tls: return "TLS";
        case InstallFailureKind::MediaFire: return "MEDIAFIRE";
        case InstallFailureKind::Space: return "SPACE";
        case InstallFailureKind::Storage: return "STORAGE";
        case InstallFailureKind::ArchiveCorrupt: return "ARCHIVE";
        case InstallFailureKind::ArchiveUnsafe: return "UNSAFE";
        case InstallFailureKind::Unsupported: return "FORMAT";
        case InstallFailureKind::Installer: return "INSTALLER";
        default: return "OTHER";
    }
}
inline bool isDownloadFacingFailure(InstallFailureKind kind) {
    switch(kind) {
        case InstallFailureKind::Network:
        case InstallFailureKind::HttpMissing:
        case InstallFailureKind::HttpDenied:
        case InstallFailureKind::HttpBusy:
        case InstallFailureKind::HttpServer:
        case InstallFailureKind::Tls:
        case InstallFailureKind::MediaFire:
            return true;
        default: return false;
    }
}
} // namespace ui
} // namespace psvitaalive
