#!/usr/bin/env python3
"""Checked one-shot PSVitaAlive UI patch. Removed after CI VitaSDK build."""
from pathlib import Path
p=Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
s=p.read_text(encoding="utf-8")
def change(old,new):
    global s
    if s.count(old)!=1:
        raise RuntimeError(f"Expected unique anchor, got {s.count(old)}: {old[:100]!r}")
    s=s.replace(old,new,1)

# Fifth per-app button: explicitly opt in to deep size measuring.
change("                for(int i=0;i<4;i++){\n"
       "                    if(hit(px,py,ox+24+i*204,oy+378,195,52)){",
       "                for(int i=0;i<5;i++){\n"
       "                    if(hit(px,py,ox+24+i*164,oy+378,154,52)){")

# Both index refresh (quick) and explicit size scan can be cancelled.
change("if(!moved && installedDataAction_==-1 && py>oy+h-55)",
       "if(!moved && installedDataAction_<0 && py>oy+h-55)")

# Known Game/Data installation invalidates only its Title ID's local
# observations. Keep VPK-only and data revision tracker independent.
change(
    "    // Record only if all selected Game/Data ZIP steps completed successfully.\n"
    "    // The VPK receipt is independent, and no failed/cancelled data batch is marked current.",
    "    // A successful Game/Data extraction may change files even if dr did\n"
    "    // not change. Invalidate ONLY the installed-directory index for it.\n"
    "    if (!aborted && installAllRevisionHasFiles_ && installAllRevisionFilesOk_)\n"
    "        ::psvitaalive::storage::InstalledDataManager::invalidateCachedTitle(\n"
    "            installAllJobItem_.titleId);\n"
    "    // Record only if all selected Game/Data ZIP steps completed successfully.\n"
    "    // The VPK receipt is independent, and no failed/cancelled data batch is marked current."
)
p.write_text(s,encoding="utf-8")
print("Applied catalog-safe directory-index UI integration")
