# Custom UI fonts (dynamic)

Any **.pgf** file in these folders appears in Settings -> UI Font:

| Path | When |
|------|------|
| Client PSVitaAlive/assets/font/ | Packed into the VPK as app0:font/ |
| ux0:data/psvitaalive/fonts/ | On the memory card (no rebuild) |

## Names

Use any filename, e.g. MyCoolFont.pgf, arcade.pgf, sans.pgf

The selector lists Default plus every .pgf found (both folders).

## Bundle with the VPK

1. Convert TTF/OTF to PGF.
2. Copy into Client PSVitaAlive/assets/font/
3. Rebuild the VPK (CMake packs assets/font -> app0:font/).

## Convert TTF/OTF to PGF (not TIFF)

Fonts are TrueType (.ttf) / OpenType (.otf), not TIFF images.

Tool: https://github.com/PSP-Archive/ttf2pgf

    ./ttf2pgf MiFuente.ttf MiFuente.pgf 20

Then place MiFuente.pgf in assets/font/ or ux0:data/psvitaalive/fonts/
