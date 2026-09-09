# Plugin updates (planned)

> **Status:** documented for later — **not implemented yet**.  
> Goal: allow PSVitaAlive to detect outdated essential (and catalog) plugins and offer reinstall/update without shipping a new VPK every time a plugin binary changes.

## Current behaviour (what works today)

| Piece | Behaviour |
|--------|-----------|
| Catalog links `type: Plugin` | Download to `extract_path`, append `line` under `section` in `config.txt`, show reboot modal |
| Essential plugins (startup modal) | Hardcoded list: `kubridge`, `fd_fix`, `libshacccg` + fixed URLs |
| “Installed” check | File present + (for taiHEN) line present in `config.txt` |
| Kubridge special case | File **size** fingerprint: TheFlow v0.1 = `5075` bytes → treated as **missing**; bythos14 v0.3.1 Hotfix = `11630` bytes → OK |
| Install All | Installs Plugin links after VPK / Game Files / Data Files; skips if already “installed” |
| VPK / ZIP / PKG flows | Unrelated; must stay untouched |

Limitation: only kubridge has an outdated-size rule. Other plugins never “update” once the file exists.

## Problem to solve

When a recommended plugin binary changes (e.g. new kubridge, new fd_fix):

1. Users who already have the old file are **not** prompted again.
2. Shipping a new client VPK only to change a URL/size is heavy.
3. Catalog authors cannot declare “this Plugin link expects size X / version Y”.

## Design options (from analysis)

### A — Remote manifest for essential plugins (recommended phase 1)

Host a JSON on the repo or Archive.org, e.g.:

`https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/plugins/essential_plugins.json`

Example shape:

```json
{
  "updated_at": "2026-09-09",
  "plugins": [
    {
      "id": "kubridge",
      "name": "kubridge.skprx",
      "version": "0.3.1-hotfix",
      "url": "https://github.com/bythos14/kubridge/releases/download/v0.3.1_hotfix/kubridge.skprx",
      "extract_path": "ur0:tai/",
      "section": "*KERNEL",
      "line": "ur0:tai/kubridge.skprx",
      "size": 11630,
      "sha256": "optional…",
      "outdated_sizes": [5075]
    }
  ]
}
```

**Client:**

- After catalog load (same window as today’s essential prompt), fetch manifest (cache under `ux0:data/psvitaalive/`).
- Compare on-disk file to `size` / `outdated_sizes` / optional `sha256`.
- Missing or outdated → same essential-plugins modal (install one by one → reboot).
- If fetch fails → **fallback** to the current hardcoded list (no regression offline).

**Pros:** update plugins without a new VPK.  
**Cons:** must maintain the JSON when binaries change.

### B — Version fields on catalog `Plugin` links

Extend generator + schema:

- `version` (string, optional)
- `expected_size` (number, optional)
- `sha256` (string, optional)

**Client:** button shows Installed only if file matches size/hash; otherwise allow download/update. Install All reinstalls only when outdated.

**Pros:** reuses existing Plugin pipeline; per-app plugins can version.  
**Cons:** does not alone cover global essentials unless they live as Plugin links somewhere.

### C — Hybrid (medium-term)

1. Manifest (A) for system essentials.  
2. `expected_size` / `version` on catalog Plugin links (B).  
3. Shared helper: `pluginNeedsInstall(paths, meta)`.

## Detection practical notes (Vita)

`.skprx` / `.suprx` rarely expose a clean public version API.

Preferred order:

1. `sha256` when available (strict).
2. `expected_size` + `outdated_sizes[]` (same approach as current kubridge check; cheap and proven).
3. Optional binary string scrape (fragile; avoid as primary).

## Integration rules (do not break)

- Overwrite plugin **file** on update; **append** config line only if missing (never wipe other plugins’ lines).
- Keep reboot modal + “hold L to disable plugins” messaging.
- Do not change VPK / ZIP / PKG / Adrenaline ISO-folder logic.
- Essential prompt still after theme picker + News.
- Settings INFO panel should reflect outdated vs OK when versioning exists.

## Suggested implementation order (when resumed)

1. Extract shared `isPluginUpToDate(paths, expectedSize, outdatedSizes)`.
2. Add `plugins/essential_plugins.json` + client fetch + cache + hardcoded fallback.
3. Point kubridge/fd_fix/libshacccg at the manifest (URLs + sizes).
4. Later: generator fields `expected_size` / `version` for catalog Plugin links.
5. Document maintainer steps: upload binary → update JSON sizes/URL → commit (no VPK required).

## Related code (today)

- Essential list + kubridge size check: `Client PSVitaAlive/source/ui/full_catalog_screen.cpp` (`tryShowEssentialPluginsPrompt`, `isKubridgeFileCurrent`, `essentialPluginFullyInstalled`)
- Plugin install + `config.txt`: install controller + `tai_config_editor`
- Detector (presence only): `plugin_detector.cpp` / `plugin_detector.hpp`
- Web generator Plugin fields: `web/tools/app-generator/app-generator.js`

## Decision log

| Date | Note |
|------|------|
| 2026-09-09 | Analysis written; **deferred** — no client code for remote manifest yet. Kubridge TheFlow→bythos size check already shipped separately. |
