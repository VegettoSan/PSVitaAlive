# PSVitaAlive — Multi-language support

> **Status:** Architecture approved; implementation not started yet.  
> **Scope:** Native PS Vita client UI strings only. Catalog / external data is **not** translated.  
> **Branch policy:** Changes for this feature go directly to `main`; do not create feature branches for the implementation.

## Goal

The client must support multiple interface languages without coupling the UI to individual translations.

Required behaviour:

1. Detect the system language from the PS Vita / PSTV settings.
2. If that language is supported, use it automatically.
3. If it is not supported, use **English**.
4. Allow the user to override the automatic language from **Settings**.
5. Provide a **System / Automatic** option that returns control to the console language.
6. Persist the user's language mode/selection in the existing client configuration.
7. Never load every language into RAM at once.
8. Make adding a new language mostly a matter of adding a translation file and registering its system-language mapping.
9. Missing individual translations must fall back to English instead of showing an empty string.
10. Catalog data remains untouched and is never translated by this system.

## Approved language-selection model

The language preference has two modes:

- **System / Automatic** — detect the Vita language at startup and use it when supported; otherwise use English.
- **Manual** — explicitly select one of the languages bundled with the client.

Conceptually the persisted configuration is:

```json
{
  "language_mode": "system"
}
```

or:

```json
{
  "language_mode": "manual",
  "language": "es"
}
```

The exact JSON representation may be adjusted during implementation if it remains backward-compatible with existing `config.json` files.

### Selection priority

```text
Manual selection
      ↓
explicit supported language

System / Automatic
      ↓
detect Vita system language
      ↓
Is it supported?
   /          \
 yes           no
  ↓             ↓
that language  English
```

Changing the language in Settings should take effect without requiring a catalog refresh. If the current UI architecture makes an immediate full redraw unsafe, the implementation may apply the change on the next screen transition or application restart, but this must be documented and validated before release.

## Recommended architecture

```text
PS Vita system language
        ↓
LanguageDetector
        ↓
internal Language enum / stable language ID
        ↓
Language selection policy
(System or Manual)
        ↓
LocalizationManager
        ↓
current language table + English fallback
        ↓
UI uses TextId → localized string
```

The UI must not contain language-specific conditionals such as:

```cpp
if (language == "es") ...
else if (language == "fr") ...
```

Instead, UI code should request stable translation identifiers:

```cpp
drawText(L(TextId::Search));
drawText(L(TextId::Settings));
drawText(L(TextId::Download));
```

The localization layer owns the mapping from `TextId` to translated text.

## System-language detection

The current client already uses VitaSDK's system parameter API to obtain the Vita language for Common Dialog configuration:

```cpp
sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, ...);
```

Therefore the localization implementation should reuse the same Vita system-language source rather than introducing a second platform-specific detection mechanism.

The detector should convert the Vita numeric/system value into an internal stable language identifier. The rest of the client must not depend directly on Sony/VitaSDK numeric language constants.

Example conceptual mapping:

```text
SCE system language value
        ↓
LanguageDetector
        ↓
Language::English
Language::Spanish
Language::French
Language::German
...
```

Unsupported system languages resolve to `Language::English`.

The exact list and numeric mappings must be verified against the VitaSDK headers/runtime before implementation; do not guess or hardcode unverified values.

## Translation files

Preferred runtime asset layout:

```text
assets/lang/
├── en.lang
├── es.lang
├── fr.lang
├── de.lang
└── ...
```

The `.lang` files are UTF-8 and use simple `key=value` entries.

Example:

```text
SEARCH=Search
SETTINGS=Settings
DOWNLOAD=Download
CANCEL=Cancel
```

Spanish:

```text
SEARCH=Buscar
SETTINGS=Ajustes
DOWNLOAD=Descargar
CANCEL=Cancelar
```

The exact parser rules must be defined before implementation, including handling of:

- UTF-8 text;
- blank lines;
- comments;
- `=` inside translated values;
- duplicate keys;
- malformed lines;
- trailing whitespace;
- newline conventions.

A malformed translation file must never prevent the client from starting. English remains the final fallback.

## RAM and loading

- Keep **one active language table** in memory for the session.
- Do **not** load all language files simultaneously.
- Parse the selected `.lang` file once and release its temporary file buffer.
- English may be kept as the fallback table if required by the implementation; memory usage must be measured on Vita/Vita3K before deciding whether to keep both tables resident.
- Adding more languages should primarily increase VPK/assets size, not active runtime memory proportional to the total number of languages.

## English fallback

English is the mandatory base language and must always be complete.

Fallback happens at two levels:

### Language fallback

```text
Vita language = Japanese
Japanese translation unavailable
        ↓
English
```

### Per-key fallback

```text
Spanish selected
        ↓
Does Spanish contain TextId::Download?
      /       \
    yes        no
     ↓          ↓
 Spanish     English
```

No missing translation should result in an empty UI label.

For supportability, logs and diagnostic messages may remain in English and do not need to be localized unless explicitly decided later.

## Settings integration

The existing client already persists settings in:

```text
ux0:data/psvitaalive/config.json
```

and `AppSettingsData` currently contains install, PSP/PS1, media, theme and other preferences. Language selection should be added to this existing settings mechanism rather than creating another configuration file.

The Settings UI should expose:

```text
Language
────────────────────
System / Automatic
English
Español
Français
...
```

When System / Automatic is selected, the UI may display the resolved language for clarity, for example:

```text
Language       System (Español)
```

When Manual is selected:

```text
Language       Español
```

The language names shown in the selector should preferably be the languages' own names (`English`, `Español`, `Français`, etc.) rather than translating those names through the current UI language.

## What is translated

Only **client interface/chrome** is localized, including where applicable:

- navigation labels;
- Settings labels and descriptions;
- search UI;
- category/filter UI labels;
- buttons and actions;
- download/install progress messages;
- dialogs and confirmation prompts;
- error messages shown to the user;
- update/restart messages;
- plugin prompts;
- favorites/status labels;
- empty/loading states;
- other visible client-generated UI text.

## What is NOT translated

Catalog fields from GitHub remain exactly as stored:

- application `name`;
- `description`;
- `long_description` when present;
- author names;
- category/subcategory names from catalog data;
- changelogs;
- requirements;
- link names/types supplied by catalog data;
- external source information.

The localization system must not modify `apps/`, `authors/`, `categories/`, generated catalogs, import data, or the website.

## Dynamic strings

Dynamic UI messages must use translation templates rather than concatenating localized fragments manually.

Avoid:

```cpp
text = tr("Downloaded") + " " + size + " " + tr("MB");
```

Prefer a stable message key/template capable of receiving values, for example conceptually:

```text
DOWNLOAD_PROGRESS=Downloaded {current} of {total}
```

The final formatting API should be selected after auditing the existing text-rendering helpers and Vita memory constraints.

## Text ID rules

Translation keys/IDs must be:

- stable;
- descriptive;
- independent of the displayed English wording;
- unique;
- reused wherever the same UI concept is displayed.

Prefer:

```text
settings.language
settings.color_theme
common.cancel
download.start
download.cancelled
install.failed
catalog.loading
```

over keys based directly on English text:

```text
"Cancel"
"Download"
"Settings"
```

This allows English wording to change without invalidating the identifier used by other translations.

## Font / Unicode

The first implementation should target Latin-script languages, including Spanish accents.

Before adding Cyrillic, Greek, Arabic, CJK or other scripts, perform a dedicated font/glyph/RAM audit for the actual Vita rendering path (`vita2d` / PGF resources).

A language must not be advertised as supported until its characters render correctly on both real Vita hardware and Vita3K where practical.

## Adding a new language

The intended workflow for a contributor should be approximately:

1. Copy `assets/lang/en.lang`.
2. Translate every required key.
3. Give the file the agreed language code/name.
4. Register the language in the localization language registry.
5. Register its Vita system-language mapping if the Vita supports that language.
6. Build and test the client.
7. Test manual selection.
8. Test System / Automatic detection when possible.
9. Test missing-key fallback to English.
10. Validate text fitting at 960×544.

Adding a language must **not** require editing every UI screen.

## Current client integration points

The current client already has a centralized settings model:

```text
Client PSVitaAlive/include/installer/app_settings.hpp
Client PSVitaAlive/source/installer/app_settings.cpp
```

and persists the settings to:

```text
ux0:data/psvitaalive/config.json
```

The current UI settings are implemented in the catalog UI and already expose install method, PSP/PS1 target, PSP media, color theme and related options. The localization setting should integrate into this existing Settings flow rather than creating a parallel settings screen.

The client also already reads `SCE_SYSTEM_PARAM_ID_LANG` in `main.cpp` for Common Dialog initialization. The localization detector should reuse that platform API/source.

## Compatibility and migration

Existing users must not lose settings when the language fields are introduced.

If an old `config.json` has no language fields:

```text
missing language configuration
        ↓
System / Automatic
        ↓
detect Vita language
        ↓
supported → that language
unsupported → English
```

Existing theme, installer, PSP/PS1, plugin and update settings must remain unchanged.

If a language file is removed from a future build while a user had manually selected it, the client must safely fall back to English and recover to a valid persisted setting.

## Suggested rollout

### Phase 1 — Audit

- Inventory all visible hardcoded UI strings.
- Inventory existing text-rendering helpers.
- Verify VitaSDK language constants and runtime values.
- Audit current font/PGF glyph coverage for English + Spanish.
- Identify dynamic strings that require formatting.

### Phase 2 — Localization core

- Add `Language` identifiers.
- Add `LanguageDetector`.
- Add `LocalizationManager`.
- Add English translation file.
- Add system/manual selection policy.
- Add config persistence.
- No broad UI migration yet.

### Phase 3 — Controlled UI test

Migrate a small, low-risk portion of Settings first:

- Language;
- Color theme;
- existing Settings labels/options.

Validate on Vita3K and real hardware before continuing.

### Phase 4 — Spanish

- Add `es.lang`.
- Test automatic detection.
- Test manual override.
- Test English fallback per key.
- Test persistence and restart.

### Phase 5 — Progressive UI migration

Migrate screen-by-screen, validating each stage:

```text
Settings
↓
Catalog/navigation
↓
App detail
↓
Download/install
↓
Dialogs
↓
News/plugin/update UI
↓
remaining visible strings
```

### Phase 6 — Additional languages

Only after the base system is stable, add more Latin languages and later perform separate font work for non-Latin scripts.

## Validation requirements

Before considering the feature complete, verify:

- fresh install with English Vita;
- fresh install with Spanish Vita;
- unsupported Vita language → English;
- System / Automatic mode;
- manual English;
- manual Spanish;
- switching back to System;
- persistence after restart;
- missing translation key → English;
- malformed translation file → English/client still starts;
- old `config.json` → no settings lost;
- all existing settings still work;
- catalog content remains unchanged;
- downloads/install/update/plugin flows remain unchanged;
- no measurable regression in startup/RAM that is unacceptable for Vita;
- text fits correctly at 960×544.

## Decision

The previous proposal of external `.lang` files remains approved, but the design is now expanded with **automatic system-language detection plus a persistent manual override**.

The authoritative model for implementation is:

```text
assets/lang/*.lang
        ↓
Language registry
        ↓
System detector + manual override
        ↓
LocalizationManager
        ↓
TextId + English fallback
        ↓
PS Vita client UI
```

The client remains the only affected component. The official catalog architecture is unchanged:

```text
apps/ + authors/ + categories/
        ↓
GitHub Actions
        ↓
catalog.json + authors.json + categories.json
        ↓
Web + Client
```

Do not modify generated catalogs or external source data to implement localization.
