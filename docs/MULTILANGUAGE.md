# PSVitaAlive — Multi-language support

> **Status:** localization core is implemented and used throughout the native client. Eight language packs are currently bundled with the VPK. Some older/residual UI strings may still fall back to English until they are migrated to localization keys.
> **Scope:** native PS Vita client UI strings only. Catalog / external data is **not** translated.

## Current packaged languages

The VPK currently ships these files under `app0:lang/`:

| Code | Language |
|---|---|
| `en` | English |
| `es` | Español |
| `fr` | Français |
| `de` | Deutsch |
| `it` | Italiano |
| `pt-PT` | Português (Portugal) |
| `pt-BR` | Português (Brasil) |
| `ru` | Русский |

The internal `Language` registry also defines Dutch, Korean, Traditional Chinese, Simplified Chinese, Finnish, Swedish, Danish, Norwegian, Polish and Turkish so system-language mapping can remain stable and future packs can be added without redesigning the client.

A language is considered available to the UI only when its `.lang` file can actually be loaded. Merely existing in the enum does not make it selectable.

## Required behaviour

1. Detect the system language from PS Vita / PSTV settings.
2. If a matching installed translation pack exists, use it automatically.
3. If it is unavailable, fall back to **English**.
4. Allow a manual language override in Settings.
5. Provide **System / Automatic** to return control to the console language.
6. Persist mode/selection in the existing `config.json`.
7. Do not load every language table into RAM simultaneously.
8. Missing individual keys fall back to English.
9. A missing/malformed optional language pack must not prevent startup.
10. Catalog metadata remains untouched.

## Architecture

```text
PS Vita system language
        ↓
languageFromSystemValue()
        ↓
Language enum
        ↓
System or Manual selection
        ↓
LocalizationManager
        ↓
active table + English fallback table
        ↓
TextId / string key → UI text
```

Primary files:

```text
Client PSVitaAlive/include/localization/language.hpp
Client PSVitaAlive/source/localization/language.cpp
Client PSVitaAlive/include/localization/localization.hpp
Client PSVitaAlive/source/localization/localization.cpp
Client PSVitaAlive/assets/lang/*.lang
```

## System / Automatic and Manual modes

Settings persist language preference in the existing client configuration.

Conceptually:

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

Selection priority:

```text
Manual
  ↓
requested code exists and pack is loadable?
  ├─ yes → requested language
  └─ no  → recover through System/English behaviour

System / Automatic
  ↓
read SCE_SYSTEM_PARAM_ID_LANG
  ↓
map to internal Language
  ↓
translation pack available?
  ├─ yes → mapped language
  └─ no  → English
```

The system-language mapping is centralized in `language.cpp`; UI screens should not depend on Sony numeric language constants directly.

## Runtime loading and RAM

`LocalizationManager` loads:

- the English table as the fallback;
- one active table for the selected language.

It does **not** keep all language packs resident in memory.

`availableLanguages()` probes packs using the same loader, so Settings can expose only languages that are actually present in the VPK.

Adding more language packs increases VPK asset size, but normal runtime localization memory does not grow linearly with every installed language table.

## Translation file format

Files live in:

```text
Client PSVitaAlive/assets/lang/
```

and are packaged to:

```text
app0:lang/
```

Format is UTF-8 `KEY=value`.

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

Current parser behaviour:

- blank lines are ignored;
- lines beginning with `#` are comments;
- the first `=` separates key/value;
- leading/trailing spaces, tabs and CR are trimmed;
- duplicate keys resolve to the last parsed value;
- malformed lines without a valid key/value separator are skipped.

## Fallback behaviour

### Unsupported or unavailable language

```text
Vita system language maps to Korean
        ↓
ko.lang not packaged
        ↓
English
```

### Missing key in an installed pack

```text
Spanish active
        ↓
key present in es.lang?
  ├─ yes → Spanish value
  └─ no  → en.lang value
```

If English itself is missing a key, `LocalizationManager::get()` returns the key name rather than an empty pointer/string. This keeps failures visible during development.

## Startup ordering

Localization must be available before any startup task that displays user-facing text.

Current order in `main.cpp` is intentionally:

```text
Storage / installer settings
        ↓
LocalizationManager::initialize(settings)
        ↓
FullCatalogScreen initialization / startup UI
        ↓
startup update phase
        ↓
CatalogManager init
        ↓
ImageCache startup maintenance
        ↓
normal catalog loading
```

This ordering was moved earlier specifically so image-cache maintenance can show translated progress before the normal catalog begins loading.

## Image-cache startup strings

The image-cache v3 maintenance phase uses these private keys:

```text
IMAGE_CACHE_LABEL
IMAGE_CACHE_CHECKING
IMAGE_CACHE_CLEANING
IMAGE_CACHE_READY
```

They are shown while the cache is checked and, when over the disk threshold, while old complete image files are removed.

All **currently packaged languages** have a compatibility translation for these four strings in the localization layer:

```text
English
Español
Français
Deutsch
Italiano
Português (Portugal)
Português (Brasil)
Русский
```

### Why these strings currently have a code fallback

The feature was added after the existing `.lang` packs. To avoid shipping startup text in English for non-English users, `L(const char* key)` first asks the normal active/English tables and then uses `startupImageCacheTextFallback()` only when the key was not found.

Priority is therefore:

```text
active .lang value
        ↓ if missing
English .lang value
        ↓ if missing
startup image-cache compatibility fallback
        ↓ otherwise
raw key
```

This is intentionally compatible with the normal data-driven model: if the same `IMAGE_CACHE_*` key is later added directly to a `.lang` file, that file automatically wins and no cache code change is required.

See [`IMAGE_CACHE.md`](IMAGE_CACHE.md) for the complete cache behaviour.

## What is translated

Client-generated visible text should use localization where practical, including:

- navigation labels;
- Settings labels/descriptions;
- search/filter UI;
- embedded QWERTY keyboard titles, placeholders, instructions and action captions (not the physical English keymap);
- buttons/actions;
- loading/startup states;
- download/install progress;
- confirmation/error dialogs;
- update/restart UI;
- plugin prompts;
- status labels;
- theme UI;
- image-cache startup maintenance.

Diagnostic logs do not need to be translated.

## Embedded keyboard: localized interface, English key layout

As of the [native IME replacement decision (20 October 2026)](KEYBOARD_NATIVE_IME_REPLACEMENT_20261020.md), PSVitaAlive draws its own `SearchKeyboard` using the current theme/font. The **keyboard itself is English QWERTY on every language**, with a `123` page and extra `MORE` symbols. No Japanese/Chinese/Cyrillic composition or predictive input method is implemented. Changing the UI language **does not** switch the character layout.

The titles and surrounding buttons do use the normal `LocalizationManager::get` lookup, from the currently selected `app0:lang/*.lang` table:

| Key | Displayed context |
|---|---|
| `KEYBOARD_SEARCH_TITLE` | Catalog search modal title |
| `KEYBOARD_ZIP_TITLE` | Custom ZIP extraction path modal title |
| `KEYBOARD_PLACEHOLDER` | Empty text field |
| `KEYBOARD_NAV_HINT` | D-pad / X / Square / Start quick guide |
| `KEYBOARD_TOUCH_HINT` | Touch and hold-to-delete hint |
| `KEYBOARD_SHIFT` | Right-side persistent SHIFT toggle |
| `KEYBOARD_DELETE` | Right-side delete button (tap or hold) |
| `KEYBOARD_CANCEL` | Right-side cancel button |
| `KEYBOARD_CONFIRM` | Right-side confirm button |
| `KEYBOARD_SPACE` | Space key caption |
| `KEYBOARD_MORE` | Second symbol page caption |

The currently packaged **`en`, `es`, `fr`, `de`, `it`, `pt-PT`, `pt-BR` and `ru`** packs include these eleven keys. Standard `active_ → english_ → raw key` lookup applies. Test label width in the 91 px-wide right action rail (the code reduces font scale to fit), and validate glyph coverage of both PGF and custom fonts.

Do not translate or reorder individual A–Z key positions; uppercase/lowercase visibility follows sticky SHIFT instead. Search names and other catalog content remain untouched.

## What is not translated

Catalog/external data stays exactly as authored:

- application/game names;
- descriptions / long descriptions;
- author names;
- catalog category/subcategory text;
- changelogs;
- requirements;
- catalog-provided link labels/types;
- external-source metadata.

Localization must not rewrite:

```text
apps/
authors/
categories/
catalog.json
authors.json
categories.json
commercial catalogs
external source files
```

The official catalog architecture is unchanged.

## Adding a new language

1. Copy `assets/lang/en.lang`.
2. Translate required keys.
3. Save it using the stable language code (`nl.lang`, `ko.lang`, etc.).
4. Confirm the language already exists in `Language` / `languageCode()` or add it there if truly new.
5. Confirm the Vita system-language mapping when applicable.
6. Ensure the required glyphs render with the selected UI font path.
7. Build the VPK and verify the file is packaged under `app0:lang/`.
8. Test **System / Automatic** when possible.
9. Test manual selection and persistence.
10. Test missing-key fallback.
11. Test startup flows, including image-cache maintenance text.
12. Validate text fitting at 960×544 on Vita3K and, where possible, real hardware.

Adding a language should not require editing every UI screen.

## Font / glyph note

The language registry includes non-Latin scripts, but a pack should not be shipped merely because its enum value exists. Font/glyph coverage must be validated for the actual runtime font configuration.

This is especially important for:

- Korean;
- Chinese Traditional / Simplified;
- other scripts outside the Latin/Cyrillic coverage of the selected font.

## Dynamic strings

Prefer complete localized templates over concatenating translated fragments in an English word order.

Avoid conceptually:

```cpp
text = L("DOWNLOADED") + size + L("OF") + total;
```

Prefer a single stable message/template whenever the formatting layer supports it.

## Text-key rules

Keys should be:

- stable;
- descriptive;
- independent from exact English wording;
- reused for the same UI concept;
- added to English first so the fallback remains complete.

`TextId` should be preferred for established static UI concepts. String keys are acceptable for narrow compatibility/startup surfaces where adding a broad enum dependency is unnecessary, but they must still follow the same fallback rules.

## Validation checklist

Before releasing localization changes, verify:

- fresh startup with English;
- startup with each packaged language where practical;
- System / Automatic mapping;
- unavailable system language → English;
- manual language selection;
- persistence after restart;
- missing key → English;
- missing optional pack → English;
- malformed line does not crash parser;
- catalog content remains unchanged;
- startup self-update text still renders;
- image-cache checking/cleaning/ready text is localized;
- downloads/install/update/plugin flows remain unchanged;
- no unacceptable startup/RAM regression;
- text fits the 960×544 UI.

## Catalog architecture remains unchanged

```text
apps/ + authors/ + categories/
        ↓
GitHub Actions
        ↓
catalog.json + authors.json + categories.json
        ↓
Web + Client
```

Localization is a client-only presentation layer and must never be implemented by modifying generated catalogs.

## Catalog install/update badges

Catalog cards expose one installation-state badge only, in the top-right corner. The old icon-corner duplicate is intentionally not rendered.

The badge text is part of the client localization layer:

```text
BADGE_INSTALLED
BADGE_UPDATE
```

`Installed` and `InstalledUnknown` both use `BADGE_INSTALLED` on compact catalog cards; the detailed information panel may still explain that the installed version is unknown. `UpdateAvailable` uses `BADGE_UPDATE`.

Card layout reserves the computed badge width before drawing the app title. If either the app title or a localized badge does not fit its allotted width, the existing clipped marquee renderer is used instead of allowing text to overlap other metadata.

The update badge keeps the existing orange warning identity and adds a lightweight breathing outer glow derived from the frame clock. The badge itself never disappears, no texture is allocated, and no additional worker/timer is created.
