# Estabilidad del instalador — fase 2: espacio en disco

19 de octubre de 2026. Esta fase no modifica la detección de versiones ni los JSON generados.

## Motivación
Antes, `InstallController::requestInstall` pedía cerca de 2,1 veces el tamaño esperado en ux0 incluso para archivos ZIP comprimidos y PKG en cola del sistema BGDL. Además, `main.cpp` usaba `item.size` del VPK principal cuando un enlace secundario Game/Data ZIP carecía de `link.size`, bloqueando instalaciones con datos erróneos.

## Comportamiento nuevo
- **PKG gestionado por BGDL:** no exige el preflight local de 2,1x; el servicio de consola maneja la operación. No se altera la selección de BGDL, zRIF ni su flujo.
- **ZIP con destino:** preflight únicamente para guardar el **archivo comprimido** en ux0, más un margen de 32 MiB + 5%. Esto no certifica espacio suficiente para extraer: `ZipExtractor` conserva su escaneo de tamaños descomprimidos, métodos y margen de seguridad, *después* de descargar y *antes* de escribir archivos. Si no hay espacio de extracción, el ZIP puede conservarse mediante el flujo de recuperación existente.
- **VPK/PKG PSP directo:** conserva el presupuesto conservador anterior de 2,1x.
- **Plugin:** comprueba archivo descargado en ux0 más 4 MiB de margen; errores de instalación ur0 siguen gestionados en la copia.
- **Tamaños de enlaces:** `link.size` tiene prioridad; `item.size` solo se usa cuando la URL coincide con el enlace principal. Si el tamaño del secundario es desconocido, no se inventa ni se reutiliza el tamaño del VPK: se permite comenzar y el proveedor puede devolver Content-Length/Content-Range en la descarga. El gestor de descargas conserva sus controles de integridad.

## Validaciones
`tests/test_install_space_policy.cpp` comprueba clasificación, desbordamientos de 64 bits y tamaños de enlaces. GitHub Actions mantiene los tests previos y el build VitaSDK.

Pendiente probar en consola: ZIP comprimido frente a descomprimido, instalaciones con poco espacio, SD2Vita, PKG BGDL, ISO/CSO, Game/Data Files sin tamaño declarado, instalación de plugins en ur0 y recuperación ZIP.

## Pendientes independientes
La detección de Kubridge por tamaño no autentica la versión: requiere hashes verificados de los binarios oficiales y política para forks. HTTPS en VitaSDK tiene verificación deshabilitada por compatibilidad deliberada; no se reactiva sin una estrategia de certificados y pruebas en hardware. Consultar `docs/NETWORK_TLS.md`.

La investigación APP_VER/VERSION/hashes quedó **solo documentada** en `docs/FUTURE_EXTERNAL_VERSION_IDENTIFICATION_20261019.md`: ninguna funcionalidad de versiones se implementó aquí.
