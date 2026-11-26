# Storage manager: active catalog

The existing game installation index (237878f, 7d234a1) is connected to the storage manager; no generated catalogs or release version are modified.

- Homebrew keeps its declared ux0:data roots, VPK uninstall, historical size cache, and ZIP recovery.
- Vita games use the current catalog's regional Title IDs and LiveArea packages.
- PSP and PS1 reuse the installed-media index on ux0/uma0/imc0/xmc0. PSP accepts identified ISO/CSO or commercial PBP; PS1 accepts identified PS1 PBP (including embedded PSN content ID). Unrecognized media or arbitrary filenames are not deletion targets.
- Non-Homebrew tabs exclude saved ZIPs from scan, rows, totals and cleanup, even with a stale action. Temporary files and protected caches remain shared.
- Game deletion uses the existing explicit confirmation and promoter API or exact, freshly revalidated Adrenaline paths. No parent media directory, savedata, or unrelated Homebrew root is accepted. No automatic game deletion.
- Commercial scans do not overwrite the existing Homebrew historical directory index. Unknown game directory sizes are measured sequentially by the existing worker; ISO/CSO sizes come from stat.
- Regression: all four catalogs, regional IDs, multiple mounts, cross-catalog path rejection, ZIP preservation, existing Homebrew behavior. On physical Vita confirm both D-pad and touch, game/package-only and Adrenaline-only, deletion result and refresh, empty lists, and a retained ZIP surviving cleanup from all three game tabs.

CI and hardware validation are separate; source integration does not establish a successful VitaSDK build or physical test.
