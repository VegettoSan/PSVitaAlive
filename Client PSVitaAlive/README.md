# PSVitaAlive — Cliente nativo (VitaSDK)

Cliente Homebrew de [PS Vita Alive Store](https://github.com/VegettoSan/PSVitaAlive) para PS Vita / PSTV.

## Qué hace hoy

- Catálogos JSON publicados por el repo (`catalog.json`, etc.) — **sin** hablar con VitaDB/NeoVitaDB directamente.
- Descarga HTTP de VPK / PKG / ZIP.
- Instalación VPK vía Promoter Utility + verificación de árbol en `ux0:app/`.
- UI de catálogo, detalle, progreso y **resultado claro** (éxito / error + LiveArea).
- Logs de sesión en `ux0:data/psvitaalive/logs/session.log` (se reinician cada vez que abres la app).

## Compilar

```bash
export VITASDK=/usr/local/vitasdk   # o tu ruta
cd "Client PSVitaAlive"
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

Salida: `PSVitaAlive.vpk` (TitleID `PSVA00001`).

## Datos en la Vita

```text
ux0:data/psvitaalive/
  logs/session.log     # log de la sesión actual (reset al boot)
  logs/install.log     # detalle de instalación
  cache/               # catálogos e imágenes
  tmp/                 # staging temporal
```

## Módulos (código)

| Carpeta | Responsabilidad |
|---------|-----------------|
| `source/catalog/` | Parseo y gestión del catálogo |
| `source/network/` | HTTP + cola de descarga |
| `source/installer/` | VPK/PKG/ZIP → disco / LiveArea |
| `source/ui/` | vita2d, overlays, navegación |
| `source/storage/` | Rutas y FS del proyecto |
| `source/archive/` | Detección de formato + ZIP |

Cada subcarpeta importante tiene un `README.md` técnico breve.

## Requisitos en consola

- CFW (HENkaku / Enso / h-encore) + VitaShell.
- Unsafe homebrew habilitado.
- Para PKG comerciales con licencia: plugin **NoNpDrm** (el cliente no implementa DRM).

## Contribuir / reutilizar

El diseño separa **catálogo (pipeline CI)** de **cliente (instalación y UI)**. Si te basas en este trabajo:

1. Lee los README de `source/installer/` y `source/ui/`.
2. No acoples el cliente a APIs externas de catálogo.
3. Conserva la verificación post-promote de LiveArea en VPK.
4. Mantén el log de sesión corto (reset al inicio).
