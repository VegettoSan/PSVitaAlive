# PSVitaAlive — Decision record: replace the native Sony IME with an embedded keyboard

**Date:** 20 October 2026 (local development/testing); commits may be dated 21 October in UTC  
**Status:** **Implemented in `main`; tested and accepted on a physical PS Vita for catalog search**  
**Scope:** PSVitaAlive native client input UI only — Homebrew/commercial catalog JSON schemas, website, update pipeline, download/install worker and VitaSDK renderer are unchanged.  
**Final reference implementation:** [`14d5471`](https://github.com/VegettoSan/PSVitaAlive/commit/14d547197486feb01df190fceaad0137668b7268)

> **Resumen de la decisión:** Se abandonó el teclado nativo de Sony **para introducir texto en PSVitaAlive** porque repetidas pruebas en una Vita real terminaron en crashes de `ScePaf` al abrirlo varias veces. Tras experimentar con idiomas, tipo de IME, trazas, GPU y sincronización de imágenes, no se demostró una corrección estable. Se implementó un teclado propio QWERTY con `vita2d`, controles y pantalla táctil, integrado en los temas y el idioma de la interfaz. Esto **no** significa que PSVitaAlive haya eliminado `ScePaf` de las demás funciones del sistema, ni que el origen del crash fuese probado como un bug de Sony.

## Decision and context

The earlier client used the PS Vita system IME for catalog searches and custom ZIP extraction destinations. Its lifecycle included `sceImeDialogInit`, `sceImeDialogGetStatus`, `sceImeDialogGetResult`, `sceImeDialogTerm`, optional `sceImeDialogAbort`, and per-frame `vita2d_common_dialog_update`. Repeated openings on real hardware produced either a black, unresponsive IME screen or a process crash.

Across multiple diagnostic builds, PS Vita `.psp2dmp` files consistently implicated the `SceCommonDialogPafMain` thread in `ScePaf`, at module-relative offset **`0x13D8`**. The absolute PC changed between launches, as expected with module load addresses. Many sessions succeeded before a later opening failed; RAM was not visibly exhausted. This is a repeatable *crash location*, not proof of which PSVitaAlive call, VitaSDK configuration or OS implementation detail first triggered it.

### Controlled experiments and results

| Investigation | Observation |
|---|---|
| Lower GPU texture pressure, cancel image requests around IME opening | Failures persisted. |
| Force vs. automatically select IME language; vary `supportedLanguages` | Failures persisted. |
| Switch `SCE_IME_TYPE_DEFAULT` to `SCE_IME_TYPE_BASIC_LATIN` (VitaDB-Downloader comparison) | Failures persisted. |
| Add synchronous IME/GPU/CommonDialog tracing and recovery combination L + R + START | Produced useful diagnostics, but intensive synchronous trace I/O introduced noticeable opening stalls. Lighter logging removed those stalls without curing the crash. |
| Pause/cancel ImageCache, wait for worker quiescence, synchronize GPU and defer IME opening | A crash was reproduced **after** `image_worker_idle`, `gpu_release_complete` and successful IME initialization; the image-worker race was not confirmed as the cause. |
| Consider early loading of internal `ScePaf` by updater cleanup | Plausible hypothesis, **not isolated or verified as the cause** before the switch. |

A comparison with VitaDB-Downloader/VitaForge informed IME experiments but did not yield a hardware-validated fix for PSVitaAlive. **Do not claim that Sony, VitaDB, ImageCache or `sceAppUtilInit` was proven responsible.**

### Decision

Stop invoking Sony's native IME for PSVitaAlive text entry and use an **in-app, single-UI-thread QWERTY keyboard rendered using the existing `vita2d` frame**. Avoid introducing vitaGL, SDL, separate fonts/textures, new server dependencies or catalog fields.

The trade-off is intentional: this is an **English-layout input device** rather than a full Japanese/Chinese IME or arbitrary system keyboard. Its surrounding labels are localized, but advanced composition and prediction are not implemented.

## Current implementation (source of truth)

| File / API | Responsibility |
|---|---|
| `Client PSVitaAlive/include/ui/search_keyboard.hpp` | `SearchKeyboard` state, keyboard palette, public `open/update/drawOverlay/takeResult` API |
| `Client PSVitaAlive/source/ui/search_keyboard.cpp` | QWERTY/123/MORE layouts, persistent SHIFT, controller/analog/touch navigation, hold-to-delete, modal animation, localized labels |
| `Client PSVitaAlive/source/ui/full_catalog_screen.cpp` | `openSearchKeyboard`, `completeSearch`, `promptTextBlocking`, `updateAndDraw` input capture and `finishUiFrame` overlay integration |
| `Client PSVitaAlive/source/main.cpp` | Search callback and ZIP destination picker both route into `FullCatalogScreen`; pauses image network requests while the keyboard is visible |
| `Client PSVitaAlive/assets/lang/{en,es,fr,de,it,pt-BR,pt-PT,ru}.lang` | `KEYBOARD_*` text displayed in the selected client language |
| `Client PSVitaAlive/CMakeLists.txt` | Includes `source/ui/search_keyboard.cpp`; no `SceIme_stub` dependency |

```text
Search button / custom ZIP destination
            |
   FullCatalogScreen
     |         |
     |      promptTextBlocking(...)   [ZIP picker]
     |
   openSearchKeyboard(...)            [catalog search]
            |
   SearchKeyboard::open(...)
            |
   update()  <--- D-pad / left stick / buttons / front touch
            |
   FullCatalogScreen::draw() + finishUiFrame()
            |                  |
            |       SearchKeyboard::drawOverlay(...)
            |                  |
            +--------- vita2d end + one swap
            |
   takeResult(text, accepted)
            |
   accepted? apply search / return custom path : preserve previous state
```

### Layout, interaction and rendering

- **960×544**, QWERTY English layout, plus `123` and `MORE` symbol pages. The keyboard's character layout does not change with client language.
- `SHIFT`, `DEL`, `CANCEL`, `OK` stay in a **right-hand vertical action rail** on all input pages.
- The calculated left/right inner margins are both **22 px**; the action rail has **91 px** width and a **7 px** separation from the character rows. Compile-time assertions prevent overlap and unequal margins.
- Character labels show the actual case to be typed. **SHIFT is sticky**: press the onscreen SHIFT key or **L** to enable; press again to disable. Opening a fresh keyboard resets it to lowercase.
- The keyboard panel uses `BG`, `SURFACE`, `SURFACE2`, `BORDER`, `TEXT`, `DIM` and `ACCENT` of the current theme. The catalog remains behind a **semi-transparent dark overlay**; there is **no separate black clear** and no keyboard-specific GPU texture.
- `drawOverlay` is rendered **inside the catalog's existing frame**, before its single `vita2d_end_drawing` + `vita2d_swap_buffers`. This preserves modal semantics and avoids an extra buffer swap.
- Open and close slide animations use a smoothstep interpolation lasting approximately **230 ms**.
- When the modal is active, it owns controller/touch events; normal catalog input/animation and image texture preparation are skipped. The underlying catalog is drawn without reacting to keyboard navigation.

### Controls

| Control | Action |
|---|---|
| D-pad **or left analog stick** | Navigate rows/keys; same repeat handling (dead-zone thresholds: <=64 or >=192 on `lx/ly`) |
| **X** / touch key | Activate selected key / type a character |
| **L** / onscreen SHIFT | Toggle persistent upper/lower case |
| **Triangle** / `123` or `ABC` | Switch alphabet vs. numbers/symbols |
| `MORE` | Switch symbol subpage |
| **Square** / `DEL` | Delete one character on tap; while held, begin auto-delete after approximately **420 ms**, then once every **105 ms** until released |
| **Start** / `OK` | Confirm input and close |
| **Circle** / `CANCEL` | Close without applying input |
| Front touch | 1920×1088 touch coordinates mapped to 960×544 key positions; long-press delete is supported |

Current input buffer limit is **128 bytes** (`kMaxInputBytes`). No autocorrect, predictive text, candidate windows, non-Latin composition or keymap switching is implemented. The application may preserve an existing UTF-8 search string, but the on-screen letter layout intentionally offers English characters only.

### Localization

Only keyboard **interface labels** are localized: search title, ZIP extraction title, placeholder, instructions, SHIFT, DEL, CANCEL, OK, SPACE and MORE. The `LocalizationManager::get` lookup reads `KEYBOARD_*` keys from the eight installed `.lang` packs with the normal English fallback. Do not translate `QWERTY` key identities or external catalog text. Adding a language requires translated keys and glyph validation, **not** rebuilding the keyboard layout.

### The Sony dependency distinction

- PSVitaAlive's current source does **not** call `sceImeDialogInit/GetStatus/GetResult/Term/Abort` or `vita2d_common_dialog_update` for text entry.
- Removed as active UI dependencies: `ImeController`, synchronous `ime_diagnostics` module, UI IME frame and IME-specific image-quiescence waiting.
- The build still links **`SceCommonDialog_stub`** because the linked **static `vita2d` library** references `sceCommonDialogUpdate` internally. **A link-time dependency is not a runtime call by PSVitaAlive.**
- The application can still load/use **internal `ScePaf` and promoter utilities for unrelated install/update operations**. Removing those would break independent functionality and was not part of this decision.
- An archived `.restore/` code snapshot may contain the superseded IME path for history. Do not treat it as current runtime code or resurrect it into `main`.

## Validation and regression checklist

**Completed at decision time:** VitaSDK/GitHub Actions build of the custom-keyboard revisions; iterative physical PS Vita user testing of search, touchscreen/controller navigation, case changes, the themed modal and the final non-overlapping layout. The user confirmed the keyboard works and approved its UI. This acceptance is not a claim that every ZIP path variant, locale/font choice or Vita3K configuration has been exhaustively tested.

For future changes:

1. Open/close search repeatedly from stationary **and scrolling** catalog views; never invoke the system IME.
2. Confirm/cancel an existing query and verify results are only applied on confirmation.
3. Navigate every key including the right rail using D-pad **and left stick**; check analog drift/repeat thresholds.
4. Test touch typing, all `123/MORE` symbols, `SHIFT` persistence, and both short/held delete via touch, Square and X on DEL.
5. Check animations and backdrop under each active theme; no keyboard-specific black full-screen clear.
6. Switch each packaged language and check labels/text fit, font fallback and unchanged English character layout.
7. Exercise **custom ZIP destination** selection (including `ux0:data/`, `:`, `/`, dots), cancel and confirm; preserve quick-path choices.
8. Test scrolling, images and installer after closing the modal. Ensure no duplicate `vita2d` swap or new per-frame filesystem/network work.
9. Build with VitaSDK and validate the VPK. Keep `SceCommonDialog_stub` unless `vita2d` itself no longer needs it.
10. Do **not** change generated catalogs, import rules, source JSON, installer/promoter lifecycle or update-helper logic as part of keyboard maintenance.

## Historical commits (in order)

| Revision | Role |
|---|---|
| [`049f89a`](https://github.com/VegettoSan/PSVitaAlive/commit/049f89a2e1e16646c983bc25c3534d1a60923444) | Native IME diagnostic/recovery experiment |
| [`9223771`](https://github.com/VegettoSan/PSVitaAlive/commit/9223771a00d5a88767555e5398d0c2a776b4020c) | Automatic language flag experiment |
| [`4130326`](https://github.com/VegettoSan/PSVitaAlive/commit/4130326071ab56fa8020b9bd8ce224e3b7096d11) | Unforced language mask + lighter trace |
| [`3d34abd`](https://github.com/VegettoSan/PSVitaAlive/commit/3d34abd5c4c1876342fb209d22983dbb3080af36) | BASIC_LATIN IME experiment |
| [`db76890`](https://github.com/VegettoSan/PSVitaAlive/commit/db768906426c6fbf6396bab1afcc3e90483aa291) | Worker/GPU quiescence before native IME |
| [`894ecee`](https://github.com/VegettoSan/PSVitaAlive/commit/894eceecb12a117d01b77b058ed94746bfca0c7f) | First embedded QWERTY integration |
| [`da4aad2`](https://github.com/VegettoSan/PSVitaAlive/commit/da4aad24edea8636098bf78424109112427ca262) | Remove obsolete IME diagnostics; retain necessary `vita2d` link |
| [`4985751`](https://github.com/VegettoSan/PSVitaAlive/commit/4985751c2d4fa12f0a262e0398fa70bb6311da0c) | Localized themed modal with right rail |
| [`14d5471`](https://github.com/VegettoSan/PSVitaAlive/commit/14d547197486feb01df190fceaad0137668b7268) | Symmetric margins, persistent SHIFT and left-stick navigation |

**Decision reversal rule:** Do not reinstate Sony IME for searches merely because one test build opens successfully. A new native-IME option needs a separate isolated branch, repeatable real-hardware stress tests and demonstrably no regression. The embedded keyboard remains the supported client input path.
