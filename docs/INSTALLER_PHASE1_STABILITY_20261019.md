# PSVitaAlive — Estabilidad del instalador, fase 1 (2026-10-19)

## Alcance y compatibilidad
Cambios controlados en el cliente PS Vita. No se modifica ningún JSON de `apps/`, `authors/` o `categories/`, los catálogos generados, la web, la ruta normal de Download/Mirror, PKG/BGDL o ISO/CSO.

## 1. Recuperación manual ZIP
La extracción de ZIP guardados llama a `InstallDispatcher` con `forceZipExtract=true` y conserva la ruta elegida. Aunque `FormatDetector` detecte una estructura parecida a VPK, **el ZIP guardado no se promociona como VPK**. Ante error o cancelación se conserva el ZIP original. Una cancelación voluntaria se presenta como Cancelled, no Failed.

## 2. Sustitución segura de archivos al extraer
Si el ZIP contiene una entrada que sobrescribiría un archivo existente, `ZipExtractor` ahora escribe primero en un archivo temporal **en la misma carpeta**. Solo tras completar la lectura, comprobar CRC/cierre y verificar el tamaño:
1. Renombra el archivo anterior a un respaldo temporal único `.bak`.
2. Renombra el archivo temporal completo al nombre definitivo.
3. Borra el respaldo únicamente si el paso anterior funcionó.

Si falla el paso 2 se intenta restaurar el anterior desde su respaldo, y el error indica la ubicación del respaldo si no es posible. Si falla la descompresión o se cancela antes de promover, se borra solo el temporal; **el archivo anterior no se trunca**. Los archivos nuevos conservan la escritura directa existente por rendimiento y sus restos parciales se retiran ante error.

**Importante:** protección por archivo, **no** transacción atómica de todo el ZIP. Si falla una entrada posterior, otras entradas ya sustituidas permanecen actualizadas. Un corte eléctrico entre los renombrados puede dejar un `.bak` recuperable; no se debe borrar ciegamente.

## 3. Cancelación cooperativa
La señal de cancelación de `DownloadJob` es atómica compartida; no se pierde al copiar/mover trabajos ni se restablece en los reintentos. Las pausas externas de reintento la comprueban cada 100 ms. El instalador tiene una señal atómica separada, pasada a `InstallDispatcher` durante instalación/extracción, incluso ZIP offline. No se matan hilos a la fuerza. Algunas operaciones del sistema (BGDL y Promoter) no son cancelables una vez entregadas a la consola. Durante la copia final de plugins no se anuncia cancelación: esa ruta no tiene callback seguro y mantiene su comportamiento anterior.

## 4. Política de reintentos
`InstallDispatchResult::InvalidArchive` identifica layouts VPK inválidos o entradas ZIP inseguras/inválidas. Se detienen sin repetir cinco veces lo que no puede corregirse solo. Fallos transitorios de almacenamiento, Promoter o E/S conservan el manejo anterior de reintentos.

## Pruebas automatizadas
`.github/workflows/client-build.yml` ejecuta las regresiones `test_install_archive_policy.cpp` y `test_install_retry_policy.cpp`, además de compilar el VPK con VitaSDK.

## Matriz de validación pendiente en consola real
- Game Files/Data Files con `extract_path` y selección manual, junto con ZIP guardados detectados erróneamente como VPK.
- ZIP que reemplaza archivo existente (éxito, archivo corrupto y cancelación), comprobando que el archivo anterior permanece íntegro ante error.
- ZIP con archivos nuevos, rutas profundas y ZIP64/2+ GB; falta de espacio durante la extracción.
- Cancelar durante la descarga y durante la espera entre reintentos; volver a instalar después.
- Install All: VPK -> Game Files -> Data Files; PKG de Vita por BGDL, PSP/PS1 e ISO/CSO; plugins.
- VPK inválido sin eboot.bin o param.sfo: error permanente sin cinco reintentos.
- Fallos transitorios de Promoter/FS: siguen pudiendo reintentarse.

Una compilación correcta de CI **no equivale a validación de extracción en PS Vita real**.
