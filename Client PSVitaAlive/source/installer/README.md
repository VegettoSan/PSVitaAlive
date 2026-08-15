# `source/installer/` — Instalación en el cliente

Módulos que convierten un archivo local (tras la descarga) en contenido instalado en la Vita.

## Piezas

| Archivo | Rol |
|---------|-----|
| `install_controller.cpp` | Orquesta **download → install**. Expone `InstallStatus` a la UI. |
| `install_dispatcher.cpp` | Elige instalador según formato (VPK / PKG / ZIP / …). |
| `homebrew_installer.cpp` | VPK: extract → FakePackageBuilder → `scePromoterUtilityPromotePkg` (sync) → verifica `ux0:app/<TITLE_ID>`. |
| `vita_installer.cpp` | PKG: staging + Promoter (sin lógica de DRM/RIF propia). |
| `fake_package_builder.cpp` | Metadatos mínimos (`head.bin`) para que el Promoter acepte homebrew. |

## Flujo de resultado (UI)

Tras éxito o error, el controlador **no** vuelve a `Idle` de inmediato:

1. Estado `Completed` o `Failed` con `message`, `installPath`, `titleId`, `liveAreaOk`.
2. La UI muestra panel grande de éxito (verde) o error (rojo).
3. El usuario pulsa **○** (`acknowledgeResult`) o expira un timeout (~8 s).

`liveAreaOk` es `true` solo cuando, tras el promote de un VPK, existen:

- `ux0:app/<TITLE_ID>/`
- `.../sce_sys/param.sfo`
- `.../sce_sys/icon0.png`

## Logs

- `ux0:data/psvitaalive/logs/session.log` — sesión completa (se **reinicia al abrir** la app).
- `ux0:data/psvitaalive/logs/install.log` — detalle de promote/extract (también se reinicia al abrir).

## Qué no hace (aún)

- Bypass de DRM / generación de licencias (NoNpDrm es responsabilidad del sistema).
- Instaladores PBP / ISO / CSO.
- BGDL (descarga en segundo plano del sistema).
