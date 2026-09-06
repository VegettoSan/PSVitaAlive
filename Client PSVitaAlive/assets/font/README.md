# Custom UI fonts (PGF)

PSVitaAlive loads **PlayStation Graphics Font (`.pgf`)** files for the
Settings → "UI Font" selector.

## Where to put fonts

| Priority | Path | When to use |
|----------|------|-------------|
| 1 | `ux0:data/psvitaalive/fonts/` | Drop-in on device, no rebuild |
| 2 | `app0:font/` (this folder, packed into the VPK) | Bundle with the app |

Required file names (match the Settings options):

```
serif.pgf
sans.pgf
serif_bold.pgf
sans_bold.pgf
```

If a file is missing, the app falls back to `vita2d_load_default_pgf()`.

## Important: TTF, not TIFF

Fonts are **TrueType (`.ttf`)** or **OpenType (`.otf`)**, not TIFF images.

## Convert TTF/OTF → PGF

vita2d uses **PGF**, not TTF directly (the whole UI draws with `vita2d_pgf_*`).

### Tool

[ttf2pgf](https://github.com/PSP-Archive/ttf2pgf) (PSP-era converter; output works with `vita2d_load_custom_pgf` on Vita)

Build needs FreeType 2.x, then:

```bash
# Typical usage — height in pixels (try 18–24 for Vita UI)
./ttf2pgf MyFont-Regular.ttf sans.pgf 20
./ttf2pgf MyFont-Bold.ttf    sans_bold.pgf 20
./ttf2pgf MySerif-Regular.ttf serif.pgf 20
./ttf2pgf MySerif-Bold.ttf    serif_bold.pgf 20
```

Optional face flags (tool-dependent): embolden `b`, italic `i`, horizontal scale `h1.0`, etc.

### Alternative: PVF (system shell fonts only)

Sony shell fonts under `sa0:data/font/pvf/` are essentially **renamed `.otf`**.
That path is for replacing **system** fonts (with extra plugins), **not** for
`vita2d_load_custom_pgf`. PSVitaAlive expects **`.pgf`**.

### License

Only ship fonts you are allowed to redistribute inside a VPK.
