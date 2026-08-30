#!/usr/bin/env python3
"""A: remove services/img. B: more HTTP attempts for real icon0 URLs only."""
from __future__ import annotations
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
IC = ROOT / "Client PSVitaAlive/source/ui/image_cache.cpp"


def main() -> int:
    t = IC.read_text(encoding="utf-8")
    if "IMAGE_HTTP_ATTEMPTS = 8" in t and "servicesUrl" not in t and "archiveOrgServicesImgUrl" not in t:
        print("already applied A+B")
        return 0

    t = t.replace(
        "constexpr int IMAGE_HTTP_ATTEMPTS = 4;",
        "constexpr int IMAGE_HTTP_ATTEMPTS = 8; // more tries so icon0 lands in cache (no wrong fallback)",
        1,
    )

    t2 = re.sub(
        r"// archive\.org/services/img/[\s\S]*?static bool urlLooksLikeCatalogIcon\([\s\S]*?return false;\n\}\n",
        "/* services/img removed — IA page thumb is often a screenshot, not icon0 */\n",
        t,
        count=1,
    )
    if t2 != t:
        t = t2
    else:
        t = re.sub(
            r"static std::string archiveOrgServicesImgUrl\([\s\S]*?return false;\n\}\n",
            "/* services/img removed */\n",
            t,
            count=1,
        )

    new_dl = (
        "/* A: only real catalog URL (icon0/shots). B: IMAGE_HTTP_ATTEMPTS retries to fill cache. */"
        "HttpResult r=http.downloadToFile(job.url,job.path,0,onProgress,shouldCancel,IMAGE_HTTP_ATTEMPTS);"
    )

    old = (
        "std::string fetchUrl=job.url;\n        std::string servicesUrl;\n        "
        "if(urlLooksLikeCatalogIcon(job.url,job.path)){\n            servicesUrl=archiveOrgServicesImgUrl(job.url);\n        }\n        "
        "HttpResult r=HttpResult::NetworkError;\n        "
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
    if old in t:
        t = t.replace(old, new_dl, 1)
    elif "servicesUrl" in t:
        start = t.find("std::string fetchUrl=job.url;")
        if start < 0:
            start = t.find("std::string servicesUrl;")
        end = t.find('if(job.url.find("archive.org")', start)
        if start >= 0 and end > start:
            t = t[:start] + new_dl + t[end:]
        else:
            raise SystemExit("could not locate servicesUrl download block")

    if "servicesUrl" in t or "archiveOrgServicesImgUrl" in t:
        raise SystemExit("still has services/img leftovers")

    if "IMAGE_HTTP_ATTEMPTS = 8" not in t and "IMAGE_HTTP_ATTEMPTS = 4" in t:
        t = t.replace("IMAGE_HTTP_ATTEMPTS = 4", "IMAGE_HTTP_ATTEMPTS = 8", 1)

    IC.write_text(t, encoding="utf-8")
    print("OK: A services/img removed, B IMAGE_HTTP_ATTEMPTS=8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
