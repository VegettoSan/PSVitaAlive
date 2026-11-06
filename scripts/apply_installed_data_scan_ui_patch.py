#!/usr/bin/env python3
"""Checked, one-time patch for the 570KB full_catalog_screen.cpp.

GitHub connector UTF-8 blob transport has a file size limit, so this script
applies small verified edits in CI. It is removed after the VitaSDK build.
"""
from pathlib import Path
p=Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
s=p.read_text(encoding="utf-8")
def replace_exact(old, new):
    global s
    count=s.count(old)
    if count!=1:
        raise RuntimeError(f"Unsafe UI anchor: count={count}, text={old[:100]!r}")
    s=s.replace(old,new,1)

# Put installed Game/Data files FIRST in UX0 storage, above temporary files.
replace_exact(
    "    storageRows_.push_back({0,-1}); // temporary files",
    "    storageRows_.push_back({5,-1}); // installed Game/Data Files, first section\n"
    "    storageRows_.push_back({0,-1}); // temporary files"
)
replace_exact(
    "    storageRows_.push_back({5,-1}); // installed Homebrew data, separate page\n",
    ""
)

# The scan overlay has a visible Cancel hint; allow touchscreen users to
# request safe cancellation without touching delete/uninstall controls.
replace_exact(
    "            if(moved||installedDataWorker_>=0)return;\n"
    "            if(installedDataConfirm_){",
    "            if(installedDataWorker_>=0){\n"
    "                if(!moved && installedDataAction_==-1 && py>oy+h-55)\n"
    "                    installedDataScanProgress_.cancelRequested.store(\n"
    "                        true,std::memory_order_release);\n"
    "                return;\n"
    "            }\n"
    "            if(moved)return;\n"
    "            if(installedDataConfirm_){"
)
p.write_text(s,encoding="utf-8")
print("Installed-data UI order and scan cancellation patched successfully")
