# PSVitaAlive — Multi-language support (future)

> **Status:** Research only. Not implemented in the client yet.  
> **Scope:** Native PS Vita client UI strings only. Catalog / external data is **not** translated.  
> **Repo action:** Documentation. No runtime change required by this file.

## Goal

1. Detect the system language from the PS Vita / PSTV settings.
2. Load the matching translation if available.
3. Fall back to **English** if the language is missing or incomplete.
4. Never load every language into RAM at once.
5. Make adding a new language mostly “add a file + map the system code”.

## Recommended architecture

```text
Vita system language
        ↓
LanguageDetector  →  code (en, es, fr, …)
        ↓
LocalizationManager loads only that language file
        ↓
UI uses TextId → localized string (English fallback per key)
```

### RAM

- Keep **one** language table in memory for the session.
- Parse the `.lang` file once at startup, then free the file buffer.
- Extra languages increase **VPK size**, not active RAM.

### Files (proposal)

```text
assets/lang/en.lang
assets/lang/es.lang
…
```

Simple `key=value` UTF-8, for example:

```text
SEARCH=Search
SETTINGS=Settings
DOWNLOAD=Download
```

### Code pattern

Prefer stable IDs over hardcoded UI strings:

```cpp
// Instead of: drawText("Search");
drawText(L(TextId::Search));
```

Logs and diagnostics can stay in English for support.

### What is NOT translated

Catalog fields from GitHub (`name`, `description`, `changelog`, link types, etc.) stay as stored. Only client chrome (buttons, modals, progress, settings, errors) is localized.

### Font / Unicode risk

Latin languages (including Spanish accents) are the first milestone.  
Cyrillic / CJK need a separate font audit (`vita2d` / PGF glyph coverage and RAM).

### Suggested rollout

1. Audit hardcoded visible strings + current font capabilities.  
2. Add `LocalizationManager` with English only (no visual change).  
3. Migrate UI screens gradually.  
4. Add Spanish and test on a system set to Spanish.  
5. Only then evaluate more languages and CJK fonts.

### Detection

Use VitaSDK system parameter for language (`SCE_SYSTEM_PARAM_ID_LANG` / equivalent in the project’s SDK), map to `en`, `es`, …, default `en`.

Manual language override in Settings is optional and **not** required for the first version.

## Decision

Viable for a future release. Prefer:

- external `.lang` files  
- one language in RAM  
- English fallback (file + per-key)  
- no catalog translation  
- progressive migration  

Do not implement until the text/font audit is done.
