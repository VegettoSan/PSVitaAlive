# PS Vita Alive Store

<p align="center">
  <img src="https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/web/assets/logo/PSVitaAlive_Store_logo_text.png" alt="PS Vita Alive Store" width="520">
</p>

<p align="center">
  <strong>A free, open catalog platform for PlayStation Vita: homebrew and commercial packages.</strong>
</p>

PS Vita Alive Store helps users discover, download and install content on PlayStation Vita. Infrastructure stays free and server-less: **GitHub**, **GitHub Actions** and **GitHub Pages**.

---

## Repository layout

| Area | Role |
|------|------|
| `apps/`, `authors/`, `categories/` | Canonical **homebrew** editable data |
| `scripts/`, `.github/workflows/` | Import, normalize, validate, generate catalogs |
| `catalog.json`, `authors.json`, `categories.json` | Generated **homebrew** public contract |
| `catalog_psvita_games.json` | Commercial **PS Vita** games (PKG / DLC / Updates / Mods) — **no** embedded zRIF |
| `catalog_psvita_games.zrifidx` | Separate **zRIF** index for Vita PKG licenses (`content_id` → zRIF) |
| `catalog_psp_games.json` | **PSP** commercial catalog |
| `catalog_ps1_games.json` | **PS1** commercial catalog |
| `web/` | Static website (GitHub Pages) |
| `Client PSVitaAlive/` | Native PS Vita client (VPK) |
| `sources/`, `catalog_overrides/` | External source config and manual enrichment |
| `docs/` | Extra documentation |

```text
apps/*.json  (+ optional external sources if enabled)
        │
        ▼
 normalize → merge → overrides → validate
        │
        ▼
 catalog.json / authors.json / categories.json
        │
        ├───────────────┐
        ▼               ▼
     Website        PS Vita client
```

Commercial catalogs (`catalog_psvita_games.json`, etc.) are maintained separately and also consumed by the website and client.

**Do not hand-edit generated `catalog.json` / `authors.json` / `categories.json`.** Edit `apps/` or overrides instead.

---

## Public catalog endpoints

### Homebrew

```text
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog.json
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors.json
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/categories.json
```

### Commercial / multi-catalog

```text
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog_psvita_games.json
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog_psp_games.json
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog_ps1_games.json
https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/catalog_psvita_games.zrifidx
```

### Native client — commercial PKG install (verified)

The **PS Vita client** installs commercial packages from **Vita Games**, **PSP**, and **PS1** catalogs through the console’s system download manager (**BGDL**), not by promoting a raw `.pkg` file:

1. Resolve zRIF / RIF for the **selected link** (per-region / per-DLC `content_id`)
2. Enqueue URL + license with the system UI (notification title = **game name**)
3. User monitors LiveArea notifications until install completes

Requires a real device with taiHEN, **NoNpDrm** (and related plugins as needed), and a client build with **UNSAFE** privileges for ShellSvc access. See [`Client PSVitaAlive/source/installer/README.md`](Client%20PSVitaAlive/source/installer/README.md).

Any HTTP client can consume these JSON files without using this store’s UI.

---

## Link types (website, app JSON & client)

Every installable or reference URL lives in `links[]` with a `type`. The **website** and the **PS Vita client** group them into sections so downloads are not mixed together.

| `type` | Typical content | Website section | Client (detail) |
|--------|-----------------|-----------------|-----------------|
| `Download` | Main `.vpk` / primary payload | Downloads | Downloads |
| `PKG` | Commercial base package | Downloads | PKG |
| `DLC` | Downloadable content | DLC | DLC |
| `Update` | Official / game updates | Updates | Updates |
| `Patch` | Patches (often treated with Updates) | Updates | Updates |
| `Data Files` | Extra data `.zip` (extract) | Data Files | Data Files |
| `Game Files` | Large game data `.zip` (extract) | Game Files | Game Files |
| `Mod` / `Mod Pack` | Mods / mod packs | Mods | Mods |
| `Mirror` | Alternate download URL | Other Links | Other |
| `Repository` / `Official Website` / `Documentation` / `Issues` / `Community` / `Other` | Informational | Other Links | Other |

**Optional link fields**

| Field | Meaning |
|-------|---------|
| `size` | Integer **bytes** (app generator can enter B/KB/MB/GB and converts) |
| `extract_path` | Where the client extracts a ZIP **without** asking (e.g. `ux0:data/`). Used for **Data Files**, **Game Files**, **Mod**, **Mod Pack**, **Patch**. If empty, the client shows the quick-path picker. |
| `recommended` | At most **one** link per app may be `true` (primary install action). |
| `content_id` | Preferred key for commercial PKG license lookup in `catalog_psvita_games.zrifidx`. |

PS Vita commercial entries may include per-SKU language notes and update PKG links from NoPayStation-style data. Homebrew records live under `apps/*.json` (see [`apps/README.md`](apps/README.md)).

---

## Clients

### Website (`web/`)

Static catalog browser: homebrew, authors, categories, commercial game pages, **App JSON generator**, and news/content driven from the repo.

### PS Vita client (`Client PSVitaAlive/`)

Native client (Title ID **PSVAS1178**). Users only need to **open the client**: if a newer **GitHub Release** exists, the app can **check, download and install the update automatically** (via helper bubble **PSVAUPDT1**). No PC required for routine updates.

**Highlights**

- Catalogs: Homebrew, Vita Games, PSP, PS1
- Search, Settings (including **color theme** palettes), touch + controls; News modal (from `news.txt`); optional Discord error **Report**
- Downloads (MediaFire CDN resolution, Archive.org, GitHub, …) with retries on slow networks
- Install: **VPK** (including nested `.vpk` inside a release ZIP), **ZIP** extract (`extract_path` or quick paths; large / >2 GB archives), licensed **PKG** via system **BGDL**
- Free-space check before large downloads (~2.1× payload)
- During download/install/extract: **screen forced on**, **PS button locked**, soft power-off menu locked (see [Downloads & installations](#downloads--installations-ps-vita-client))
- Voluntary **Download cancelled** UI (no false “Installation failed” / no Report)
- Self-update from [Releases](https://github.com/VegettoSan/PSVitaAlive/releases) (see below)
- Plugin detection (prefer `ur0:tai`); session logs under `ux0:data/psvitaalive/logs/`

#### Automatic client updates

```text
Open PS Vita Alive Store
        │
        ▼
  Check GitHub Releases /latest  (if enabled in config/settings)
        │
        ├─ up to date → load catalogs as usual
        └─ newer VPK → download → install updater PSVAUPDT1 →
              updater promotes new client → relaunches store →
              removes temporary updater bubble
```

- Toggleable (startup config / settings); safe path uses **PSVAUPDT1** because a Vita app cannot reliably promote **itself** while running.
- Manual fallback VPK: `ux0:data/psvitaalive/update/PSVitaAlive.vpk`
- Details: [`Client PSVitaAlive/README.md`](Client%20PSVitaAlive/README.md) and [`Client PSVitaAlive/source/update/README.md`](Client%20PSVitaAlive/source/update/README.md)

**Releases:** https://github.com/VegettoSan/PSVitaAlive/releases  
**Website:** https://vegettosan.github.io/PSVitaAlive/

---

## Recommended setup (real PS Vita)

For **stable downloads and HTTPS** on real hardware, configure the console as follows.

### CFW / plugins

| Recommendation | Why |
|----------------|-----|
| **HENkaku / h-encore / Enso** (as appropriate for your firmware) | Required to run homebrew and taiHEN plugins |
| **[iTLS-Enso](https://github.com/SKGleba/iTLS-Enso) — full version** | Replaces the system TLS stack so modern HTTPS (GitHub, MediaFire, Archive.org, many CDNs) works. The **full** package is preferred over minimal installs when you rely on the store client for large HTTPS transfers |
| **NoNpDrm** | Licensed Vita PKG / DLC / updates (BGDL path) |
| **NoPspEmuDrm** (kernel + user as required) | PSP / PS1 LiveArea bubbles and related content |
| Prefer **`ur0:tai`** over `ux0:tai` | Survives memory-card changes; client plugin detection prefers ur0 |

iTLS-Enso repository and releases:

- Project: [https://github.com/SKGleba/iTLS-Enso](https://github.com/SKGleba/iTLS-Enso)
- Releases: [https://github.com/SKGleba/iTLS-Enso/releases](https://github.com/SKGleba/iTLS-Enso/releases)

Install the **full** build when possible, reboot, then test HTTPS (open the client and load a catalog).

### DNS (Wi‑Fi)

Use reliable public DNS on the Vita’s Wi‑Fi connection so hostnames used by the client resolve consistently:

| Role | Address |
|------|---------|
| **Primary** | `8.8.8.8` |
| **Secondary** | `8.8.4.4` |

Path on the system UI (typical): **Settings → Network → Wi‑Fi → [your network] → Advanced settings → DNS** → set primary/secondary as above (disable “Automatic” DNS if present).

These are [Google Public DNS](https://developers.google.com/speed/public-dns). Other stable resolvers (e.g. `1.1.1.1` / `1.0.0.1`) are acceptable; the important part is **not** leaving a broken ISP DNS that fails intermittent lookups during multi‑GB transfers.

### Storage and power

- Prefer a healthy **SD2Vita / USB** setup with enough free space. The client checks free space at about **~2.1×** the expected download size before starting large jobs.
- Keep the Vita **plugged in** for multi‑GB **Game Files** / VPK installs when possible.
- Do **not** force power-off (hold power 10–30s) during an active download or extract — that truncates files and produces incomplete ZIPs.

---

## Downloads & installations (PS Vita client)

Technical overview of how the native client moves bytes and installs content. Details live under [`Client PSVitaAlive/source/installer/README.md`](Client%20PSVitaAlive/source/installer/README.md).

### Content paths

| Content | Mechanism | Notes |
|---------|-----------|--------|
| Homebrew **VPK** (or ZIP containing a `.vpk`) | HTTP download → extract if needed → `scePromoterUtility` (async promote + poll) | Work dir `ux0:data/psva_vpk` |
| **Data Files / Game Files** (ZIP) | HTTP download → integrity pre-check → extract | `extract_path` from catalog or quick-path picker; large archives supported (including **>2 GB** via libzip + custom `sceIo` source) |
| Commercial **PKG** (Vita / PSP / PS1) | License resolve → **BGDL** system queue | Progress in LiveArea notifications; not a silent “promote raw PKG” path |

### Why the client locks the shell and keeps the screen on

HTTP downloads and ZIP extraction run **inside the client process** (libcurl + worker threads). Unlike system BGDL for commercial PKG, if the process is suspended or killed mid-transfer:

- TCP streams stop and file handles can become invalid
- Partial ZIP files often lack a valid **EOCD / ZIP64** end-of-central-directory marker
- The next install attempt fails with **`zip_open` / incomplete archive** errors (download must be redone)

To reduce that class of failure, while a job is in **Downloading** or **Installing** state the client:

| Protection | API / behaviour | What the user sees |
|------------|-----------------|---------------------|
| **Anti auto-suspend** | `sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND)` on a keep-awake thread while `busy()` | Console does not idle-suspend during the job |
| **Screen stays on** | Also `DISABLE_OLED_OFF` + `DISABLE_OLED_DIMMING` while busy | Panel does not dim/blank mid-progress |
| **PS button blocked** | `sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN)` | Cannot exit to LiveArea until the job ends |
| **Soft power-off menu blocked** | `sceShellUtilLock(SCE_SHELL_UTIL_LOCK_TYPE_POWEROFF_MENU)` | Short power-hold “Power Off” menu suppressed |
| **Locks released** | `sceShellUtilUnlock` on Completed / Failed / Cancelled / shutdown | Normal PS / power behaviour returns |

**Hard force power-off** (long hold) cannot be blocked in userland — it is a system/hardware path. The UI shows a high-visibility **LOCKED** banner and toasts if the user presses START, SELECT, L/R, or other keys during a job.

### Incomplete ZIP detection

Before opening a downloaded archive, the installer can reject files that do not look complete on disk (missing EOCD / ZIP64 marker near the end of the file). That fails early with a clear error instead of a cryptic extract crash — typical after suspend, kill, or network cut mid-download.

### User-facing progress UI

During an active job the progress overlay states explicitly that:

- The **screen stays on**
- **PS button and power menu are disabled**
- Only **CIRCLE** (cancel) is expected until the job finishes

Toasts reinforce the same message if Settings, exit, or catalog switch are attempted.

### Practical recommendations during large installs

1. Start the download and **leave the client in the foreground** until it finishes or you cancel with CIRCLE.
2. Do not rely on turning the screen off to “save battery” during Game Files — the client keeps the screen on on purpose.
3. Prefer a stable Wi‑Fi link and the DNS settings above; flaky DNS is a common cause of stalled HTTPS.
4. Ensure **iTLS-Enso (full)** is installed if catalogs or CDNs fail TLS handshake.
5. After a failure mentioning incomplete ZIP / `zip_open`, **delete the partial file** (cancel already tries to clean the job) and retry on a better connection.
6. For commercial PKG, after “Queued…”, finish the install from **LiveArea notifications** — that path is system BGDL and can continue differently from in-app HTTP ZIP downloads.


## Contributing data

Prefer pull requests that edit **source** data (`apps/`, overrides, scripts) rather than generated catalogs.

1. Add or edit `apps/*.json` (and authors/categories when needed).
2. Run validation / generation scripts (or rely on CI).
3. Confirm website and client still load the public JSON endpoints.

See module READMEs under `apps/`, `scripts/`, and `docs/`.

---

## Data sources & credits

### Current situation (transition in progress)

The automatic **VitaDB** feed was removed from the default pipeline. External acquisition may still use **VitaHomebrewDB** and manual curation while the catalog moves toward independent maintenance.

### VitaDB migration note

Historical enrichment may still reflect data that originally came from public VitaDB-era indexes. Credit remains with upstream authors and catalog maintainers.

### Important ownership clarification

This store does **not** own third-party homebrew binaries, art, or trademarks. Catalog entries are discovery metadata plus links.

### What this project adds

- Aggregation, normalization, validation and category/subcategory consistency
- Manual curation and ongoing maintenance
- Public clients and documentation so others can reuse the catalog and learn from the design

### Commercial catalogs

PS Vita / PSP / PS1 package listings and license sidecars may incorporate public metadata associated with communities such as **NoPayStation** and related tools. Those files are **not** the same as the homebrew CC BY catalog; respect third-party rights and do not treat zRIF or PKG links as redistributable “owned” content of this repo.

## License and ownership

### What this project is

PS Vita Alive Store is a **discovery and download helper** for the PS Vita scene. It does **not** claim ownership of homebrew apps, plugins, ports, icons, screenshots, or other third-party works. Those belong to their **authors**. Each homebrew entry keeps author information (`author_ids` / author profiles) and, when available, links to the author’s repository or releases.

### Software (client, scripts, tooling)

| Part | Terms |
|------|--------|
| **Software** (Vita client, updater, scripts, web tooling, build system) | **[MIT License](LICENSE)** — use, copy, modify, distribute, etc., with the MIT notice |

### Homebrew catalog data

| Part | Terms |
|------|--------|
| **Homebrew catalog data** (`apps/`, `authors/`, `categories/`, generated `catalog.json`, `authors.json`, `categories.json`, and this project’s compilation/curation of that data) | **[CC0 1.0](CATALOG_LICENSE.md)** (public domain dedication) |

**Anyone may use the homebrew catalog data for any purpose**, including other stores, mirrors, apps, and commercial use. **Credit is not required.** A mention is welcome if you want to, but it is optional.

Full text: [CATALOG_LICENSE.md](CATALOG_LICENSE.md).

### Third-party rights (unchanged)

- Homebrew **binaries, source, icons, and screenshots** remain under each author’s rights and licenses.
- This repository is not a claim of ownership over those works.
- Commercial catalogs and zRIF/license sidecars are **not** covered by the CC0 catalog dedication; treat them as third-party reference data.

### Upstream catalogs

Public databases and community work (including projects associated with VitaDB, VitaHomebrewDB, and many individual authors) have helped with discovery and enrichment over time. Credit for that work stays with those projects and authors. This store’s goal is to facilitate finding and installing content, not to replace or claim the creators’ work.
