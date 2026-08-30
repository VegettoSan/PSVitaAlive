#!/usr/bin/env python3
"""Prefer archive.org/services/img for catalog icons; fewer HTTP retries for images."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HC_H = ROOT / "Client PSVitaAlive/include/network/http_client.hpp"
HC_C = ROOT / "Client PSVitaAlive/source/network/http_client.cpp"
IC = ROOT / "Client PSVitaAlive/source/ui/image_cache.cpp"


def patch_http_header() -> None:
    t = HC_H.read_text(encoding="utf-8")
    if "maxAttemptsOverride" in t:
        print("http_client.hpp: already has maxAttemptsOverride")
        return
    old = """    HttpResult downloadToFile(
        const std::string& url,
        const std::string& destinationPath,
        uint64_t resumeOffset = 0,
        HttpProgressFn onProgress = nullptr,
        HttpCancelFn shouldCancel = nullptr
    );"""
    new = """    HttpResult downloadToFile(
        const std::string& url,
        const std::string& destinationPath,
        uint64_t resumeOffset = 0,
        HttpProgressFn onProgress = nullptr,
        HttpCancelFn shouldCancel = nullptr,
        int maxAttemptsOverride = 0 // 0 = default (archive 10 / other 5); images can pass 4
    );"""
    if old not in t:
        raise SystemExit("downloadToFile decl not found")
    HC_H.write_text(t.replace(old, new, 1), encoding="utf-8")
    print("http_client.hpp: maxAttemptsOverride")


def patch_http_cpp() -> None:
    t = HC_C.read_text(encoding="utf-8")
    if "maxAttemptsOverride > 0" in t and "int maxAttemptsOverride" in t:
        print("http_client.cpp: maxAttempts already wired")
        return

    old_sig = """HttpResult HttpClient::downloadToFile(
    const std::string& url,
    const std::string& destinationPath,
    uint64_t resumeOffset,
    HttpProgressFn onProgress,
    HttpCancelFn shouldCancel
) {"""
    new_sig = """HttpResult HttpClient::downloadToFile(
    const std::string& url,
    const std::string& destinationPath,
    uint64_t resumeOffset,
    HttpProgressFn onProgress,
    HttpCancelFn shouldCancel,
    int maxAttemptsOverride
) {"""
    if old_sig not in t:
        raise SystemExit("downloadToFile definition not found")
    t = t.replace(old_sig, new_sig, 1)

    old_max = "    const int kMaxAttempts = isArchive ? 10 : 5;"
    new_max = """    const int kMaxAttempts = (maxAttemptsOverride > 0)
        ? maxAttemptsOverride
        : (isArchive ? 10 : 5);"""
    if old_max not in t:
        if "maxAttemptsOverride > 0" in t:
            print("http_client.cpp: kMaxAttempts already patched")
        else:
            raise SystemExit("kMaxAttempts line not found")
    else:
        t = t.replace(old_max, new_max, 1)
    HC_C.write_text(t, encoding="utf-8")
    print("http_client.cpp: maxAttemptsOverride wired")


def patch_image_cache() -> None:
    t = IC.read_text(encoding="utf-8")
    if "services/img" in t:
        print("image_cache: services/img already present")
        return

    old_ns = "constexpr int WORKER_PRIORITY=0x10000100,WORKER_STACK=128*1024,MAX_RETRIES=3;"
    if old_ns not in t:
        raise SystemExit("MAX_RETRIES const not found")
    helpers = r'''constexpr int WORKER_PRIORITY=0x10000100,WORKER_STACK=128*1024,MAX_RETRIES=3;
// Fewer SSL rounds per image — UI should not burn 10× curl-35 on a missing icon.
constexpr int IMAGE_HTTP_ATTEMPTS = 4;

// archive.org/services/img/<id> serves the item thumbnail without a 302 to a datanode
// (web preview path). Much friendlier for Vita SSL than /download/<id>/icon0.png.
static std::string archiveOrgServicesImgUrl(const std::string& url) {
    const char* marker = "archive.org/download/";
    const size_t pos = url.find(marker);
    if (pos == std::string::npos) return {};
    size_t idStart = pos + std::strlen(marker);
    const size_t idEnd = url.find('/', idStart);
    if (idEnd == std::string::npos || idEnd <= idStart) return {};
    const std::string id = url.substr(idStart, idEnd - idStart);
    if (id.empty() || id.find("..") != std::string::npos) return {};
    return std::string("https://archive.org/services/img/") + id;
}

static bool urlLooksLikeCatalogIcon(const std::string& url, const std::string& path) {
    // Catalog app icons land in .../app_<hash>.png; screenshots use shot_ / other prefixes.
    if (path.find("/app_") != std::string::npos) return true;
    const size_t slash = url.find_last_of('/');
    const std::string file = (slash == std::string::npos) ? url : url.substr(slash + 1);
    // common homebrew icon filenames
    if (file == "icon0.png" || file == "icon.png" || file == "icon0.jpg" || file == "icon.jpg")
        return true;
    return false;
}
'''
    t = t.replace(old_ns, helpers, 1)

    old_dl = (
        "HttpResult r=http.downloadToFile(job.url,job.path,0,onProgress,shouldCancel);"
        'if(job.url.find("archive.org")!=std::string::npos)'
        "{/* archive.org image spacing */sceKernelDelayThread(150*1000);}"
    )
    if old_dl not in t:
        old_dl2 = "HttpResult r=http.downloadToFile(job.url,job.path,0,onProgress,shouldCancel);"
        if old_dl2 not in t:
            raise SystemExit("downloadToFile call in image worker not found")
        old_dl = old_dl2

    new_dl = r'''std::string fetchUrl=job.url;
        std::string servicesUrl;
        if(urlLooksLikeCatalogIcon(job.url,job.path)){
            servicesUrl=archiveOrgServicesImgUrl(job.url);
        }
        HttpResult r=HttpResult::NetworkError;
        if(!servicesUrl.empty()){
            diagnostics::log(std::string("[ImageCache] try services/img url=")+servicesUrl);
            r=http.downloadToFile(servicesUrl,job.path,0,onProgress,shouldCancel,IMAGE_HTTP_ATTEMPTS);
            bool okImg=false;
            if(r==HttpResult::Ok){
                SceIoStat st={};
                okImg=sceIoGetstat(job.path.c_str(),&st)>=0&&st.st_size>64;
            }
            if(!okImg){
                sceIoRemove(job.path.c_str());
                diagnostics::log(std::string("[ImageCache] services/img miss, fallback download url=")+job.url);
                r=http.downloadToFile(job.url,job.path,0,onProgress,shouldCancel,IMAGE_HTTP_ATTEMPTS);
            }
        }else{
            r=http.downloadToFile(job.url,job.path,0,onProgress,shouldCancel,IMAGE_HTTP_ATTEMPTS);
        }
        if(job.url.find("archive.org")!=std::string::npos){/* archive.org image spacing */sceKernelDelayThread(150*1000);}'''

    t = t.replace(old_dl, new_dl, 1)
    IC.write_text(t, encoding="utf-8")
    print("image_cache: services/img + IMAGE_HTTP_ATTEMPTS=4")


def main() -> int:
    patch_http_header()
    patch_http_cpp()
    patch_image_cache()
    print("OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
