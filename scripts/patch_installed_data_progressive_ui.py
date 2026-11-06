#!/usr/bin/env python3
"""Checked one-shot patch for the very large Vita client UI translation unit.

Stage only; deleted after successful GitHub Actions VitaSDK build. Declared
exact anchors prevent accidental edits to unrelated touch screens.
"""
from pathlib import Path

p=Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
s=p.read_text(encoding="utf-8")
def replace(old, new):
    global s
    n=s.count(old)
    if n != 1:
        raise RuntimeError(f"Expected exactly one UI anchor, got {n}: {old[:100]!r}")
    s=s.replace(old,new,1)

# Background read-only measurement must not block touch scrolling.
replace("touchMoved_&&installedDataWorker_<0&&!installedDataDetail_&&",
        "touchMoved_&&(installedDataWorker_<0||installedDataAction_==-3)&&"
        "!installedDataDetail_&&")

# Deletion and Sony package removal keep the touch overlay locked. Auto
# measurement (-3) allows normal scrolling, selecting and returning.
replace(
    "            if(installedDataWorker_>=0){\n"
    "                if(!moved && installedDataAction_<0 && py>oy+h-55)",
    "            if(installedDataWorker_>=0 && installedDataAction_!=-3){\n"
    "                if(!moved && installedDataAction_<0 && py>oy+h-55)"
)

# Result panel must remain visible until explicit acknowledgment.
replace(
    "            if(moved)return;\n"
    "            if(installedDataConfirm_){",
    "            if(moved)return;\n"
    "            if(installedDataResultVisible_){\n"
    "                if(hit(px,py,ox+30,oy+381,w-60,62) || py>oy+h-55)\n"
    "                    finishInstalledDataResult();\n"
    "                return;\n"
    "            }\n"
    "            if(installedDataConfirm_){"
)

# Exactly three prominent delete buttons, and one separate, understated
# measure-size touch strip. Keep Circle and footer back behavior.
replace(
    "            }else{\n"
    "                for(int i=0;i<5;i++){\n"
    "                    if(hit(px,py,ox+24+i*164,oy+378,154,52)){\n"
    "                        installedDataActionFocus_=i;\n"
    "                        activateInstalledDataSelection();\n"
    "                        break;\n"
    "                    }\n"
    "                }\n"
    "                if(py>oy+h-55){",
    "            }else{\n"
    "                if(hit(px,py,ox+24,oy+341,w-48,28)){\n"
    "                    if(installedDataWorker_>=0)\n"
    "                        showToast(::psvitaalive::L(\"ID_SCAN_FINISHING\"),2800);\n"
    "                    else startInstalledDataOperation(-2);\n"
    "                    return;\n"
    "                }\n"
    "                for(int i=0;i<3;i++){\n"
    "                    if(hit(px,py,ox+24+i*272,oy+382,260,61)){\n"
    "                        installedDataActionFocus_=i;\n"
    "                        activateInstalledDataSelection();\n"
    "                        break;\n"
    "                    }\n"
    "                }\n"
    "                if(py>oy+h-55){"
)
p.write_text(s,encoding="utf-8")
print("PSVitaAlive touch integration patched and validated")
