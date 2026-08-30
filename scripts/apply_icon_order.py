#!/usr/bin/env python3
"""Use real icon0.png first; services/img only as fallback (IA thumb is often a screenshot)."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IC = ROOT / "Client PSVitaAlive/source/ui/image_cache.cpp"


def main() -> int:
    t = IC.read_text(encoding="utf-8")
    if "icon URL first" in t:
        print("already fixed order")
        return 0

    old_comment = """// archive.org/services/img/<id> serves the item thumbnail without a 302 to a datanode
// (web preview path). Much friendlier for Vita SSL than /download/<id>/icon0.png.
"""
    new_comment = """// archive.org/services/img/<id> is the item *page* thumb (often a screenshot, not icon0).
// Only used as fallback when the real icon URL fails — never as primary for catalog cards.
"""
    if old_comment in t:
        t = t.replace(old_comment, new_comment, 1)

    new2 = (
        "/* icon URL first — services/img is last resort (IA thumb is often a screenshot) */"
        "r=http.downloadToFile(job.url,job.path,0,onProgress,shouldCancel,IMAGE_HTTP_ATTEMPTS);"
        "bool okImg=false;"
        "if(r==HttpResult::Ok){SceIoStat st={};okImg=sceIoGetstat(job.path.c_str(),&st)>=0&&st.st_size>64;}"
        "if(!okImg&&!servicesUrl.empty()){"
        "sceIoRemove(job.path.c_str());"
        'diagnostics::log(std::string("[ImageCache] icon miss, fallback services/img url=")+servicesUrl);'
        "r=http.downloadToFile(servicesUrl,job.path,0,onProgress,shouldCancel,IMAGE_HTTP_ATTEMPTS);"
        "}"
    )

    old2 = (
        "if(!servicesUrl.empty()){"
        'diagnostics::log(std::string("[ImageCache] try services/img url=")+servicesUrl);'
        "r=http.downloadToFile(servicesUrl,job.path,0,onProgress,shouldCancel,IMAGE_HTTP_ATTEMPTS);"
        "bool okImg=false;"
        "if(r==HttpResult::Ok){"
        "SceIoStat st={};"
        "okImg=sceIoGetstat(job.path.c_str(),&st)>=0&&st.st_size>64;"
        "}"
        "if(!okImg){"
        "sceIoRemove(job.path.c_str());"
        'diagnostics::log(std::string("[ImageCache] services/img miss, fallback download url=")+job.url);'
        "r=http.downloadToFile(job.url,job.path,0,onProgress,shouldCancel,IMAGE_HTTP_ATTEMPTS);"
        "}"
        "}else{"
        "r=http.downloadToFile(job.url,job.path,0,onProgress,shouldCancel,IMAGE_HTTP_ATTEMPTS);"
        "}"
    )

    if old2 in t:
        t = t.replace(old2, new2, 1)
        print("replaced minified block")
    elif "try services/img url=" in t:
        start = t.find("if(!servicesUrl.empty()){")
        end = t.find("if(job.url.find(\"archive.org\")", start)
        if start < 0 or end < 0:
            raise SystemExit("could not slice block")
        t = t[:start] + new2 + t[end:]
        print("replaced by slice")
    else:
        raise SystemExit("services/img block not found")

    IC.write_text(t, encoding="utf-8")
    print("OK: icon URL first, services/img fallback only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
