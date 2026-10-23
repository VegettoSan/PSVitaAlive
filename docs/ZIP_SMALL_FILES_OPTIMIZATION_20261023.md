# PSVitaAlive — Optimización de extracción ZIP/VPK de archivos pequeños

**Fecha de validación:** 23 de octubre de 2026  
**Alcance:** cliente PS Vita (`Client PSVitaAlive/`), no catálogos ni web  
**Origen:** PR #45, rama `experiment/zip-small-files-cache-20261022`  
**Estado:** optimizaciones de fase 1 validadas parcialmente en PS Vita física y aceptadas para integración en `main`. Las pruebas pendientes se enumeran abajo.

## 1. Problema y objetivo

Los ZIP/VPK con cientos o miles de archivos pequeños producen muchas operaciones de metadatos, apertura, escritura y cierre en `ux0:` / `uma0:`. La lentitud no tiene por qué estar en la descompresión: el coste fijo por archivo se acumula.

**Objetivo de esta etapa:** disminuir operaciones repetidas en el sistema de archivos sin añadir un búfer grande, desactivar CRC o retirar las garantías de actualización/reemplazo.

## 2. Cambios integrados

### 2.1 Caché acotada de directorios

`Client PSVitaAlive/include/archive/extraction_directory_cache.hpp` implementa `ExtractionDirectoryCache` con `std::unordered_set<std::string>` y un máximo de **256 rutas** retenidas por extracción.

- El extractor comprueba primero `lastEnsuredParent` y después esta caché.
- Si una ruta no está en la caché, se mantiene la comprobación y creación mediante `StorageManager`; solo se guarda si fue preparada correctamente.
- Las entradas explícitas de directorio también aprovechan la caché.
- Al alcanzar el límite, la caché se vacía y continúa: **no es LRU**, pero acota la cantidad de entradas y evita crecimiento ilimitado con ZIP enormes.
- No se reutiliza entre instalaciones y no se omiten las validaciones previas de rutas ni la preparación de carpetas profundas.

Esto beneficia especialmente a ZIP con archivos que alternan entre subdirectorios.

### 2.2 Creación exclusiva de archivos nuevos

Para archivos de salida aún inexistentes se intenta primero:

```cpp
sceIoOpen(outPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_EXCL, 0777);
```

Si esta llamada tiene éxito, evita la consulta previa `StorageManager::exists(outPath)` (`sceIoGetstat`). El modo exclusivo no permite truncar un archivo que ya existe.

Ante un error de la llamada exclusiva, se consulta si la salida ya existe:
- **Existe:** se conserva el mecanismo de sustitución segura (escribir temporal, verificar libzip/CRC y tamaño, renombrar original a backup, promover temporal y limpiar).
- **No existe:** el extractor desactiva el intento exclusivo para el resto de esa extracción y utiliza el procedimiento anterior de creación; registra un aviso y un contador de fallback.

No se cambian el búfer de **256 KiB**, las lecturas `zip_fread`, ZIP64, fuentes segmentadas, staging de rutas profundas, comprobación del espacio libre, la cancelación, el límite de frecuencia del progreso (**250 ms**) ni las pausas cooperativas.

**Nota de compatibilidad:** el fallback es para archivos comprobados como no existentes; siempre puede haber errores de dispositivo/permisos que deban informarse. La compatibilidad de todos los dispositivos y particiones no ha sido demostrada únicamente con las pruebas siguientes.

### 2.3 Diagnóstico de bajo coste

Se añade una línea agregada al final de cada extracción, sin registrar cada archivo:

```text
[ZipPerf] entries=... dir_cache_hits=... dir_checks=... excl_created=... existing_replaced=... excl_fallbacks=... status=...
```

- `dir_cache_hits`: coincidencias con último directorio o caché; **no son todas consultas `sceIoGetstat` evitadas respecto a la versión anterior**, porque la caché de último directorio ya existía.
- `dir_checks`: verificaciones de directorio durante el bucle; no incluye el preflight previo.
- `excl_created`: archivos nuevos abiertos con creación exclusiva.
- `existing_replaced`: archivos destinados al proceso de reemplazo.
- `excl_fallbacks`: primera detección de fallo exclusivo en una ruta ausente (puede desactivarse para el resto del ZIP).
- `status`: resultado del extractor.

## 3. Verificación y resultados en PS Vita física

**Caso reportado:** instalación de Gun Bros con VPK + Game Files ZIP, probada por el mantenedor en hardware real.

Salida de la versión instrumentada de **fase 2** (la fase 2 mantiene el mismo algoritmo optimizado de fase 1 y solo añade contadores temporales):

```text
[ZipPerf] entries=11 dir_cache_hits=8 dir_checks=3 excl_created=11 existing_replaced=0 excl_fallbacks=0 status=Ok
[ZipPerf] entries=329 dir_cache_hits=315 dir_checks=14 excl_created=321 existing_replaced=0 excl_fallbacks=0 status=Ok
```

Medición agregada del ZIP de Game Files: `[ZipTiming] total_us=90643029` (**90,64 segundos**, 225.957.839 bytes escritos).

- **321 archivos nuevos** mediante `SCE_O_EXCL`, **0 fallbacks** y **0 reemplazos** en Game Files.
- **315 coincidencias** de caché/último padre y **14** comprobaciones durante el bucle.
- Las 329 entradas y la extracción del VPK de 11 entradas terminaron con `status=Ok`.
- El mantenedor confirmó que Gun Bros apareció en el LiveArea y abrió correctamente.

En una ejecución anterior de fase 1, la extracción de Game Files estuvo alrededor de **130,8 segundos**; la segunda registró **90,64 segundos**. **No debe atribuirse la diferencia del 30,7 % a un cambio adicional del algoritmo**: la fase 2 solo mide y ambos tiempos pueden variar según estado del dispositivo, caché y tarjeta de almacenamiento. La estimación previa de «casi 2 horas restantes» no es una medición del extractor antiguo.

El perfilador de fase 2 (PR #46) midió aproximadamente 30,51 s de `sceIoOpen`, 30,89 s de `sceIoWrite` y 25,49 s de `zip_fread`, datos útiles para continuar la investigación. Esos contadores finos **no se integran** en esta etapa.

## 4. Validación automatizada

El workflow `.github/workflows/client-build.yml` ejecuta `tests/test_extraction_directory_cache.cpp` en C++17 para comprobar:
- Inserción y deduplicación.
- Alternancia entre directorios.
- Límite de 256 entradas incluso ante miles de rutas distintas.

La compilación de fase 1 con la imagen oficial `vitasdk/vitasdk:2026.08` pasó y produjo un VPK en GitHub Actions:
`https://github.com/VegettoSan/PSVitaAlive/actions/runs/39902586287`.

La fase 2 también compiló y se probó en hardware real:
`https://github.com/VegettoSan/PSVitaAlive/actions/runs/39905678922`.

La compilación y la prueba funcional descritas **no equivalen a una matriz exhaustiva de regresión**.

## 5. Riesgos y pruebas pendientes

Antes de considerar completamente validada la ruta de extracción en todas las condiciones:

1. **Destino con archivos existentes:** confirmar temporal + backup + rename, preservación ante error y CRC/tamaño.
2. **Cancelación y ZIP corruptos:** revisar limpieza de nuevos archivos y preservación de originales.
3. **ZIP64/partes segmentadas** y archivos muy grandes.
4. **Rutas profundas y staging** en `ux0:` y `uma0:`; especialmente tras errores.
5. **ZIP con miles de archivos pequeños** en varias particiones/tarjetas y con una base de comparación repetible.

En caso de regresión, los cambios se pueden revertir por un commit normal: la implementación previa del extractor sigue disponible en el historial Git. No alterar los catálogos generados.

## 6. Decisión de integración

Por petición del mantenedor se integra **solo la fase 1 validada**, dejando **PR #46** (perfilado detallado) y cualquier optimización futura separados para no introducir sobrecarga adicional de medición en producción.

**Archivos funcionales afectados:** `zip_extractor.cpp`, `extraction_directory_cache.hpp`, test y CI. Sin cambios a la arquitectura `apps/ + authors/ + categories/ → GitHub Actions → catalog.json + authors.json + categories.json`.
