#!/usr/bin/env python3
"""Static UI regression: storage labels, scroll and shared-root consent.

Source-level checks cannot replace a visual PS Vita/Vita3K test.
"""
from pathlib import Path
import re

root = Path("Client PSVitaAlive")
installed = (root / "source/ui/installed_data_screen.inc").read_text(encoding="utf-8")
catalog = (root / "source/ui/full_catalog_screen.cpp").read_text(encoding="utf-8")
manager = (root / "source/storage/installed_data_manager.cpp").read_text(encoding="utf-8")
policy = (root / "include/storage/installed_data_policy.hpp").read_text(encoding="utf-8")
header = (root / "include/ui/full_catalog_screen.hpp").read_text(encoding="utf-8")

def scales(fragment):
    return [float("0" + x) for x in re.findall(r"(?<![\d.])(\.\d{2})f\b", fragment)]

def verify_scale(label, fragment, minimum):
    used = scales(fragment)
    assert used, f"{label}: no font scales were detected"
    assert min(used) >= minimum, f"{label}: tiny font scale {min(used)}"

main_storage = catalog.split("void FullCatalogScreen::drawStorageOverlay() {", 1)[1].split(
    "void FullCatalogScreen::shutdown(){", 1)[0]
inspector = installed.split("void FullCatalogScreen::drawInstalledDataOverlay() {", 1)[1]

verify_scale("Storage manager", main_storage, 0.85)
verify_scale("Installed Game/Data inspector", inspector, 0.80)

# Main storage list must be clipped, including the larger text sizes.
assert main_storage.index("vita2d_enable_clipping();") < main_storage.index(
    "vita2d_set_clip_rectangle(x+21,listTop,x+w-34,listBottom);"
)
assert main_storage.index("vita2d_disable_clipping();") > main_storage.index(
    "int cy=listTop-storageScrollY_;"
)

# Every matching app is cached when the confirmation opens, with Title ID.
assert "installedDataCatalogShares(" in installed
assert "other.titleId + \"  |  \" + other.name" in installed
assert "installedDataShareLabels_.push_back" in installed
assert "installedDataShareScroll_" in header
assert "installedDataShareLabels_" in header
assert "ID_SHARED_CATALOG_TITLE" in installed
assert "ID_SHARED_DELETE_WARNING" in installed
assert "if(n<3" not in inspector  # source uses a bounded row loop below
assert "for(int n=0;n<3 && scroll+n<total;++n)" in inspector

# Touch hitboxes and buttons must use the enlarged dialog geometry.
assert "constexpr int DW=824, DH=474, BW=244, BH=46, GAP=22;" in inspector
assert "constexpr int dialogW=824,dialogH=474,buttonW=244,gap=22;" in catalog
assert "const int buttonY=dialogY+dialogH-59;" in catalog
assert "installedDataShareScroll_=std::min" in catalog
assert "installedDataShareScroll_ = std::min" in installed

# Safety remains separate from sharing. Metadata overlaps require informed
# confirmation but do not depend on the existence of the VPK.
assert "installedDataCanOfferExternalDelete(*app)" in catalog
assert "installedDataCanOfferExternalDelete(*app)" in installed
assert "confirmedCatalogShare" in manager
assert "installedDataHasCatalogShare(snapshot, *target)" in manager
assert "targetPresentNow" not in manager
assert "(app.activeShared && !app.packagePresent)" not in policy
assert "safeExternalDataPath(p.path)" in manager
assert "scanTree(path.path" in manager
assert "scePromoterUtilityDeletePkg" in manager

# Eight existing translations must expose the new names/risks.
for name in ("en", "es", "de", "fr", "it", "pt-BR", "pt-PT", "ru"):
    text = (root / "assets/lang" / (name + ".lang")).read_text(encoding="utf-8")
    for key in ("ID_SHARED_CATALOG_TITLE", "ID_SHARED_DELETE_WARNING",
                "ID_SHARED_ACTIVE_HINT"):
        assert re.search(r"^" + key + r"=.+$", text, re.MULTILINE), (name, key)

print("Storage readability, shared-catalog disclosure and touch UI checks OK")
