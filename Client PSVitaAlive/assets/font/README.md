# Custom UI fonts (PGF + TTF/OTF)

PSVitaAlive loads fonts from:

| Path | Notes |
|------|--------|
| `Client PSVitaAlive/assets/font/` | Packed as `app0:font/` in the VPK |
| `ux0:data/psvitaalive/fonts/` | On device, no rebuild |

Supported extensions:

- **`.pgf`** — classic vita2d PGF (`vita2d_load_custom_pgf`)
- **`.ttf` / `.otf`** — FreeType via vita2d (`vita2d_load_font_file`)

Any of these files appear in **Settings → UI Font**.

## TTF/OTF (recommended if you already have TrueType)

Just copy the file, e.g.:

```
assets/font/MinSans.ttf
```

No conversion needed when FreeType is available in the VitaSDK vita2d build.

## PGF

Still supported. Convert with ttf2pgf if you prefer:

```
./ttf2pgf MiFuente.ttf MiFuente.pgf 20
```
