#!/usr/bin/env python3
"""Gentle download tuning: keep-alive + pause image cache during install downloads."""
from __future__ import annotations
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HC = ROOT / "Client PSVitaAlive/source/network/http_client.cpp"
IH = ROOT / "Client PSVitaAlive/include/ui/image_cache.hpp"
IC = ROOT / "Client PSVitaAlive/source/ui/image_cache.cpp"
MAIN = ROOT / "Client PSVitaAlive/source/main.cpp"


def patch_hc() -> None:
    text = HC.read_text(encoding="utf-8")
    old = 'headers = curl_slist_append(headers, "Connection: close");'
    new = 'headers = curl_slist_append(headers, "Connection: keep-alive");'
    if old not in text:
        if "Connection: keep-alive" in text:
            print("http_client: keep-alive already set")
            return
        raise SystemExit("http_client: Connection: close not found")
    count = text.count(old)
    text = text.replace(old, new)
    HC.write_text(text, encoding="utf-8")
    print(f"http_client: Connection keep-alive ({count} site(s))")


def patch_ih() -> None:
    text = IH.read_text(encoding="utf-8")
    if "setNetworkPaused" in text:
        print("image_cache.hpp: already has setNetworkPaused")
        return
    old = '''    // Cancel queued and active image work. The active partial file is removed
    // by the worker after libcurl acknowledges the cancellation.
    void cancelAll();
    void cancelQueuedRequests();
'''
    new = '''    // Cancel queued and active image work. The active partial file is removed
    // by the worker after libcurl acknowledges the cancellation.
    void cancelAll();
    void cancelQueuedRequests();

    /** Pause background image downloads so install/VPK bandwidth is not shared. */
    void setNetworkPaused(bool paused);
    bool networkPaused() const { return networkPaused_; }
'''
    if old not in text:
        raise SystemExit("image_cache.hpp: cancelAll block not found")
    text = text.replace(old, new, 1)

    old2 = '''    volatile bool stopping_ = false;
    volatile bool cancelRequested_ = false;
'''
    new2 = '''    volatile bool stopping_ = false;
    volatile bool cancelRequested_ = false;
    volatile bool networkPaused_ = false;
'''
    if old2 not in text:
        raise SystemExit("image_cache.hpp: flags not found")
    text = text.replace(old2, new2, 1)
    IH.write_text(text, encoding="utf-8")
    print("image_cache.hpp: patched")


def patch_ic() -> None:
    text = IC.read_text(encoding="utf-8")
    if "setNetworkPaused" in text:
        print("image_cache.cpp: already has setNetworkPaused")
    else:
        insert_fn = (
            "void ImageCache::setNetworkPaused(bool paused){"
            "if(mutex_>=0)sceKernelLockMutex(mutex_,1,nullptr);"
            "const bool changed=(networkPaused_!=paused);"
            "networkPaused_=paused;"
            "if(mutex_>=0)sceKernelUnlockMutex(mutex_,1);"
            "if(changed){"
            "if(paused)diagnostics::log(\"[ImageCache] network paused (install/download active)\");"
            "else diagnostics::log(\"[ImageCache] network resumed\");"
            "}"
            "}"
        )
        wm = "int ImageCache::workerMain()"
        if wm not in text:
            raise SystemExit("image_cache.cpp: workerMain not found")
        text = text.replace(wm, insert_fn + wm, 1)
        print("image_cache.cpp: setNetworkPaused added")

    old_loop = "while(!stopping_){Job job;bool have=false;sceKernelLockMutex(mutex_,1,nullptr);if(!queue_.empty()){"
    new_loop = (
        "while(!stopping_){"
        "bool paused=false;"
        "if(mutex_>=0){sceKernelLockMutex(mutex_,1,nullptr);paused=networkPaused_;sceKernelUnlockMutex(mutex_,1);}"
        "if(paused){sceKernelDelayThread(100*1000);continue;}"
        "Job job;bool have=false;sceKernelLockMutex(mutex_,1,nullptr);if(!queue_.empty()){"
    )
    if "if(paused){sceKernelDelayThread" in text:
        print("image_cache.cpp: worker pause already present")
    elif old_loop not in text:
        raise SystemExit("image_cache.cpp: worker loop start not found")
    else:
        text = text.replace(old_loop, new_loop, 1)
        print("image_cache.cpp: worker pause check added")

    IC.write_text(text, encoding="utf-8")


def patch_main() -> None:
    text = MAIN.read_text(encoding="utf-8")
    if "setNetworkPaused" in text:
        print("main.cpp: already pauses image cache")
        return

    old = (
        "const psvitaalive::InstallStatus cur=installer.status();"
        "using InstallState=psvitaalive::InstallStatus::State;"
        "const bool active=cur.state==InstallState::Downloading||cur.state==InstallState::Installing||"
        "cur.state==InstallState::Completed||cur.state==InstallState::Failed||cur.state==InstallState::Cancelled;"
    )
    new = (
        "const psvitaalive::InstallStatus cur=installer.status();"
        "using InstallState=psvitaalive::InstallStatus::State;"
        "images.setNetworkPaused(cur.state==InstallState::Downloading||cur.state==InstallState::Installing);"
        "const bool active=cur.state==InstallState::Downloading||cur.state==InstallState::Installing||"
        "cur.state==InstallState::Completed||cur.state==InstallState::Failed||cur.state==InstallState::Cancelled;"
    )
    if old not in text:
        raise SystemExit("main.cpp: install status block not found")
    MAIN.write_text(text.replace(old, new, 1), encoding="utf-8")
    print("main.cpp: image pause during install")


def main() -> int:
    patch_hc()
    patch_ih()
    patch_ic()
    patch_main()
    print("OK: gentle download tuning applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
