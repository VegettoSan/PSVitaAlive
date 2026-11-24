# PSVitaAlive — Minecraft Modern Legacy: investigación pendiente de extracción ZIP y bloqueos de interfaz

**Actualización:** 24 de noviembre de 2026  
**Estado:** **ABIERTO / NO RESUELTO**. No fusionar el extractor experimental ni declarar arreglado Minecraft.  
**Seguimiento técnico:** [Issue #111 — acciones pendientes](https://github.com/VegettoSan/PSVitaAlive/issues/111) · [PR #85, borrador](https://github.com/VegettoSan/PSVitaAlive/pull/85) · [Cambios estables ya integrados en PR #110](PR85_STABLE_INTEGRATION_20261124.md).

> **Seguimiento posterior — integrado parcialmente en `main`:** la ruta rápida de `SCE_O_TRUNC` para archivos existentes, comprobada en Build #645, quedó integrada mediante [PR #113](https://github.com/VegettoSan/PSVitaAlive/pull/113), commit `fb21bc0e3038d65137c7dd7b1ba6055176031485`. Ver [detalle técnico](ZIP_EXISTING_FILE_OPEN_FASTPATH_20261124.md). **No** incluye ni la reanudación por CRC32 de PR #112 ni la solución al bloqueo al crear nuevos archivos; esta incidencia permanece abierta.

## 1. Caso reproducible y síntoma

- **Aplicación:** Minecraft Modern Legacy (`LCEM00001`).
- **Acción:** extracción *offline* del ZIP conservado en `ux0:data/psvitaalive/manual/LCEM00001/Game Files for PSVA.zip.001` hacia `ux0:data/`.
- **ZIP:** 10.637 entradas; 1.203.719.020 bytes descomprimidos según el preflight. La primera parte registrada tiene tamaño 1.206.133.472 bytes en el origen; `archive=301533368` en un diagnóstico de libzip corresponde a la vista/parte reportada allí y **no debe confundirse con el total del paquete**.
- **Patrón observado:** el porcentaje por bytes se mantiene alrededor de **16–17 %**, aunque el contador de entradas sigue subiendo lentamente. El porcentaje por bytes no equivale al porcentaje de archivos.
- **Carpeta implicada:** `ux0:data/Minecraft/Common/Modern/sound/bee/`, con una gran secuencia de archivos pequeños `.pcm` de nombres diversos (por ejemplo, `nether_mob_*`, `newer_block_*`). No se verificó el número exacto de archivos que contiene el directorio ni si su clasificación es la esperada por el desarrollador.
- **Resultado:** las tres variantes de prueba volvieron a mostrar pausas muy prolongadas de interfaz; el usuario tuvo que forzar el apagado. **No hay un error final de libzip demostrado**, pues los registros se cortan con el apagado.

### Pruebas realizadas en hardware

Los números son **mediciones acumuladas hasta el último snapshot disponible** de cada prueba, no tiempos completos de instalación. Los archivos presentes al inicio variaron entre pruebas debido a las extracciones parciales anteriores; por ello, las diferencias no son un benchmark controlado.

| Prueba | Variante | Últimas entradas | Tiempo extracción | Tiempo apertura de salida | Hallazgo |
| --- | --- | ---: | ---: | ---: | --- |
| 1 | EXCL + comprobaciones originales, preflight/pacing/pipeline de PR #85 | ~6.249 / 10.637 | ~36 min 37 s | ~32 min 32 s | Aproximadamente 89 % del tiempo en aperturas. Interfaz casi inmanejable. |
| 2 · Build #644 | Fast path para `EEXIST`, evita `exists()/isDirectory()` redundantes; perfiles separados | ~6.331 / 10.637 | ~24 min 32 s | ~19 min 44 s | `excl_ms` ~18 min 54 s; `check_ms` 0. UI con bloqueos de ~47,5 s. |
| 3 · Build #645 | Apertura directa `SCE_O_WRONLY|SCE_O_TRUNC` de archivos existentes; cambia a EXCL al detectar nuevo | **6.665 / 10.637** | **33 min 43 s** | **27 min 24 s** | `direct_existing=6193`, `direct_miss=1`; crear nuevos archivos tarda ~2 s por entrada; UI con pausas >40 s. |

**Fuente directa de la tercera prueba:** `session.log` facilitado por el probador en noviembre de 2026 (no incluido en el repositorio). Los resultados de las dos primeras pruebas están resumidos en el historial/descripción del PR #85. No inferir los últimos valores de un ZIP completo: ninguno de estos intentos finalizó.

### Punto de inflexión de la tercera prueba (Build #645)

El log registra estas cifras clave:

```text
entries=6330/10637 wall_ms=1326010 file_open_ms=960874
excl_ms=0 check_ms=0 replace_ms=960863 direct_existing=6188 direct_miss=0

entries=6340/10637 wall_ms=1341559 file_open_ms=976175
excl_ms=10406 check_ms=0 replace_ms=965757 direct_existing=6193 direct_miss=1

entries=6665/10637 wall_ms=2022944 file_open_ms=1644006
excl_ms=678237 check_ms=0 replace_ms=965757 direct_existing=6193 direct_miss=1
```

- Hasta unas 6.330 entradas, `excl_ms=0`: la ruta directa evita intentos `EXCL` sobre los archivos ya existentes. **Funcionó funcionalmente como estaba previsto.**
- Alrededor del índice **6335**, el primer `O_TRUNC` sobre un archivo inexistente devuelve `ENOENT`, se registra `direct_miss=1` y el código usa `O_CREAT|O_EXCL` para nuevos archivos.
- Entre unas 6.340 y 6.665 entradas, el tiempo acumulado de apertura crece cerca de **668 segundos**; `excl_ms` aumenta de 10.406 a 678.237 ms, mientras `replace_ms` permanece fijo. Son ~325 entradas de progreso muy lento en la misma familia de rutas.
- El último reporte incluye `zip_read_ms=79.757` y `write_ms=237.301` acumulados, muy por debajo de `file_open_ms=1.644.006`; el cuello de botella observado está en operaciones de apertura/creación, **no principalmente en transferir bytes o descomprimir**.
- Se observan aperturas de salida de ~1,8 a 2,2 s (máximo registrado `max_open_ms=3466`) y `[Perf] worst_us` por encima de 40 millones (más de 40 s para un frame). El sistema registra aproximadamente **36,7 GB libres** al iniciar: falta de espacio no explica por sí sola este caso.
- Al final se habían escrito **216.466.056 de 1.203.719.020 bytes**; esto explica que el porcentaje visual rondase el 17 %, pese a haber procesado más del 62 % de las entradas.

## 2. Qué se sabe, qué no se sabe

### Confirmado por las mediciones

1. El ZIP se abre y se lee, `zip_fopen_index` y `zip_fread` avanzan; no es un bloqueo permanente en la misma entrada.
2. `sceIoOpen` para archivos del directorio `sound/bee` se vuelve progresivamente muy lento, tanto al reemplazar como al crear archivos nuevos.
3. Cambiar flags de apertura reduce algunas operaciones redundantes, pero **no elimina** el estancamiento.
4. El hilo del instalador ya trabaja separado del render; pese a ello, el acceso al almacenamiento coincide con frames de UI de decenas de segundos.
5. El extractor preserva la ruta `ux0:data/Minecraft/...`; no se debe modificar silenciosamente la estructura de recursos del juego.

### Hipótesis pendientes de comprobar

- Consultas a metadatos del sistema de archivos sobre un único directorio con muchísimas entradas causan una degradación creciente; puede depender del montaje real de `ux0:`, el medio utilizado y el estado del directorio.
- La carpeta `sound/bee` podría contener recursos agrupados de forma no óptima en el paquete original; **no está comprobado que sea un error** ni que el juego pueda cargarlos en otros directorios.
- La contención de E/S a nivel de kernel/sistema de archivos podría bloquear temporalmente la interfaz aunque el código del extractor se ejecute en otro hilo.

**No confirmado:** defecto concreto de libzip, corrupción del ZIP, deadlock formal, fallo de VitaSDK, tipo de tarjeta (`SD2Vita` frente a Sony), garantía de que reestructurar los assets sea compatible, o mejora concluyente de rendimiento entre builds con condiciones idénticas.

## 3. Código experimental: conservar, pero NO integrar todavía

**Rama:** `feature/zip-store-pipelined-extraction-20261117`  
**Última referencia verificada:** `bf078776e195abcd4395e77890b6822260f1cae8` (Build #645).

- `Client PSVitaAlive/source/archive/zip_extractor.cpp`: pipeline ZIP STORE >= 8 MiB con un hilo de escritura y búferes acotados, preflight de directorios, pausas por entradas, métricas `[ZipIO]`, rutas de apertura EXCL y directas/adaptativas.
- `Client PSVitaAlive/include/archive/zip_work_pacer.hpp`, `zip_write_pipeline_policy.hpp`, `zip_preflight_directory_policy.hpp`: políticas y helpers todavía experimentales.
- `docs/ZIP_SMALL_FILES_UI_STALL_20261113.md`, `docs/MINECRAFT_MODERN_LEGACY_ZIP_DIAGNOSTICS_20261117.md`, `docs/ZIP_STORE_PIPELINED_EXTRACTION_20261117.md`: investigación inicial; pueden continuar solo en la rama experimental.
- `Client PSVitaAlive/source/installer/install_dispatcher.cpp`: el texto `ZIP files: N/M` **sí se extrajo y llegó a main mediante #110**; no reintroducirlo en otro merge.
- La estrategia de apertura directa usa `O_TRUNC` **sin** `O_CREAT` al reemplazar. La creación `O_EXCL` intenta distinguir archivos creados por la operación para no eliminar datos previos en una limpieza. Cambiar esta lógica sin pruebas puede truncar o perder información.

La CI de Builds #644 y #645 pasó, pero el problema reprodujo en Vita real. **No reutilizar el PR #85 completo para una fusión**: tras el squash del PR #110, la rama está atrasada y debe actualizarse/recrearse desde `main` trasladando solamente lo experimental.

## 3 bis. Función aprovechable: archivos existentes frente a nuevos, y posible reanudación

**Petición de continuidad (24/11/2026):** conservar y, en una futura rama pequeña basada en `main`, evaluar por separado el comportamiento que ya mostró resultados funcionales con archivos existentes. También se plantea que, después de una interrupción, una extracción pueda continuar sin rehacer innecesariamente archivos que ya fueron extraídos.

**Aclaración fundamental: son dos funcionalidades DIFERENTES.**

### A. Lo que sí funcionó en Build #645 (detección y apertura directa)

- El extractor experimental intentaba `sceIoOpen(ruta, SCE_O_WRONLY | SCE_O_TRUNC, 0777)` **sin `SCE_O_CREAT`** al trabajar con nombres que presumía existentes. Si el archivo existe, se abre **para reemplazar su contenido**.
- Cuando retornaba `ENOENT` se cambiaba a `SCE_O_CREAT | SCE_O_EXCL` para distinguir la creación de un archivo nuevo, y registrar correctamente los archivos nuevos propiedad de la extracción.
- En los logs esta ruta registró **6.193 aperturas directas de archivos existentes**, `direct_miss=1` y `excl_ms=0` durante la primera fase. Es evidencia de que se ejecutó la ruta prevista y evitó intentos de EXCL anteriores; **no demuestra que el archivo preexistente fuera correcto ni que se ahorrara su escritura**.
- **Muy importante:** `SCE_O_TRUNC` trunca el archivo existente para volver a escribirlo, así que esto **NO es una función de «omitir los archivos ya instalados» ni una reanudación real**. Un apagado forzado durante la escritura podría dejar ese archivo incompleto. No integrar esta ruta automáticamente en `main` sin pruebas independientes de sobreescritura/cancelación/seguridad.

### B. Idea nueva por estudiar: continuar sin repetir archivos completados

Objetivo deseado: si la extracción fue interrumpida, detectar cuáles entradas del MISMO ZIP se extrajeron **completamente** en el destino y, **solo después de verificarlas**, evitar volver a escribirlas. El progreso debería reflejar tanto el trabajo confirmado como el pendiente.

Posible diseño a validar, **NO IMPLEMENTADO**:

1. Asociar una sesión de extracción recuperable al archivo ZIP concreto (identidad/fingerprint del archivo y todas las partes, tamaño de entrada, versión del manifiesto). El simple nombre del ZIP no basta.
2. Mantener un registro acotado y persistente de entradas **confirmadas como completas**: ruta normalizada, tamaño, resultado de CRC (el que corresponda al ZIP) y estado confirmado **después** del cierre exitoso. Al reiniciar, un archivo a medias nunca puede contarse como completo.
3. Antes de omitir un archivo, comprobar con una política segura que coincide con la entrada esperada. **La existencia o igualdad de tamaño por sí solas NO garantizan integridad**; un registro puede quedar desactualizado tras un apagado forzado. Evaluar coste/seguridad de una verificación por CRC o hash frente al coste del directorio grande.
4. Reutilizar primero archivos existentes **solo con confianza verificable**, sin truncarlos. Los archivos nuevos seguirían un flujo de creación y limpieza que no borre ni cambie los archivos previos del usuario.
5. Diseñar la escritura del registro para resistir apagados, con checkpoints confirmados, fallos de escritura y reemplazo de archivos. Si la identidad del ZIP cambia o hay discrepancias, **no omitir**, informar y volver al flujo seguro.
6. Mantener intactos paths que requiere Minecraft, staging de directorios profundos, ZIP64/ZIP segmentados, CRC, límite de memoria y `session.log` observabilidad. Mostrar conteos de **verificados/omitidos/extractados** sin presentar datos omitidos como bytes recién escritos.

**Riesgo técnico:** en `sound/bee` hasta una consulta de existencia/`sceIoGetstat` puede costar mucho; por tanto, comprobar miles de archivos uno por uno podría **no** mejorar el rendimiento. Antes hay que medir el coste de verificar/omitir comparado con reescribir, especialmente con archivos pequeños y directorios densos. No sustituye el plan de microbenchmark ni la protección de UI.

**Estrategia de entrega:** separar «apertura directa de existentes» y «reanudación con omisión verificada» en propuestas/PR distintos; no mezclar ambas con el pipeline ZIP y no cambiar `main` hasta que el primer paso tenga pruebas host + VitaSDK + hardware.

## 4. Plan de mejora futura por fases (sin saltarse validaciones)

### Fase A — Auditoría en PC del ZIP original (sin modificarlo)

- Inventariar entradas por directorio padre, tamaños, método STORE/DEFLATE y número de archivos en `Minecraft/Common/Modern/sound/bee/`.
- Registrar un histograma de densidad por directorio, rutas repetidas, longitudes y distribución de entradas a lo largo del índice ZIP.
- Contrastar la estructura con la fuente primaria / desarrollador del port antes de proponer reorganización. Respetar nombres y rutas que Minecraft necesita.
- No alterar el ZIP externo ni los JSON del catálogo para acomodarlos a un experimento.

**Criterio para avanzar:** tabla reproducible de densidad y confirmación de si el patrón `sound/bee` es intencional.

### Fase B — Microbenchmark de E/S aislado y seguro en PS Vita

- Ejecutar únicamente en un **subdirectorio temporal identificable y propiedad de PSVitaAlive**, con respaldo previo de datos importantes; no tocar `ux0:data/Minecraft`.
- Comparar: 100/500/1.000 archivos pequeños en **una carpeta** frente al mismo número repartido en varias carpetas; repetir con archivos existentes y nuevos. Ajustar límites a la seguridad de la prueba, sin asumir que 1.000 es inocuo.
- Medir por separado `sceIoOpen`, `sceIoWrite`, `sceIoClose`, y `sceIoGetstat` si se usa, además de latencia de UI y posibilidad de cancelar entre llamadas.
- Registrar tipo real de montaje (`ux0`/SD2Vita/Sony si el probador lo conoce), capacidad libre, cantidad de archivos previos y número de entradas nuevas.
- Aplicar límites de tiempo, número de archivos y almacenamiento; limpiar **solo** los archivos temporales explícitamente creados por la prueba. Nunca hacer limpiezas recursivas genéricas de `ux0:data/`.

**Criterio para avanzar:** curva de latencia reproducible y comparación justa entre carpetas, sin bloqueo prolongado ni apagado forzado.

### Fase C — Recuperar control de interfaz y cancelación

- Revisar qué operaciones de I/O comparten el mismo medio físico con la UI y el logger; perfilar frecuencia de actualización y contención.
- El instalador ya tiene hilo propio. Crear otro hilo o hacer `sceKernelDelayThread` después de una llamada bloqueante **no garantiza** capacidad de interrumpir `sceIoOpen`.
- La cancelación debe quedar registrada y ejecutarse **entre operaciones que hayan retornado**; investigar una mitigación segura ante llamadas largas del sistema, sin prometer cancelación inmediata si el kernel no ofrece timeout fiable.
- Conservar el ZIP recuperable, proteger datos existentes, mantener controles de CRC/ruta/staging y evitar borrados automáticos de datos del juego.

**Criterio para avanzar:** la UI permanece operable en una prueba controlada y Cancelar termina sin pérdida de archivos existentes.

### Fase D — Elegir una solución de rendimiento compatible

Evaluar solo después de A–C: advertir antes de instalar paquetes con directorios de densidad patológica; optimizar el flujo de archivos existentes; reducir trabajo extra de metadatos; o coordinar con el desarrollador una distribución de recursos diferente **si el juego soporta esos paths**. Un empaquetado alternativo por sí solo no sustituye a los archivos reales cuando el juego necesita cargarlos individualmente.

**No hacer:** aplanar o mover rutas del ZIP automáticamente, eliminar la carpeta problemática, desactivar validaciones de integridad, sobrescribir silenciosamente partidas, paralelizar agresivamente la creación de miles de archivos o insistir con la extracción completa para medir cada hipótesis.

### Fase E — Matriz de regresión y decisión de merge

- ZIP STORE pequeño, STORE de archivo grande, DEFLATE antiguo, ZIP64/segmentado, rutas profundas (staging), archivos existentes/nuevos, tarjeta llena, error de CRC y cancelación.
- Comparar con `main` en el **mismo dispositivo/medio**, con estado del directorio comparable y logs reproducibles.
- Confirmar que ni el juego, ni las partidas, ni otros archivos de `ux0:data` se pierden; comprobar además que un VPK normal sigue instalando.
- Abrir un PR **nuevo y pequeño** basado en el `main` actualizado para cada modificación validada. Solo fusionar después de CI y pruebas en Vita física.

## 5. Conservación de evidencias

En una prueba futura guardar `ux0:data/psvitaalive/logs/session.log` y `install.log` (si existe) **antes de volver a iniciar PSVitaAlive**, ya que se pueden reiniciar/sobrescribir. Archivar externamente los logs, el SHA de build, versión de VitaSDK y las condiciones de almacenamiento con la fecha de prueba.

No publicar en el repositorio el ZIP con recursos de terceros ni copias personales de datos/partidas. No editar el ZIP original para que el experimento encaje.

## 6. Definición de terminado

La incidencia **solo** podrá cerrarse cuando una PS Vita física extraiga el mismo conjunto válido sin estancarse alrededor del 17 %, el progreso y la UI sean razonablemente operables, la cancelación/recuperación estén comprobadas y no existan regresiones en otros ZIP y VPK. Una compilación verde, un 17 % alcanzado más deprisa o el mero avance del contador de entradas **no equivalen a resolución**.

**Documentación relacionada:** [integración estable #110](PR85_STABLE_INTEGRATION_20261124.md) · [reserva de espacio](PREDOWNLOAD_SPACE_RESERVATION_20261122.md) · [PR #85](https://github.com/VegettoSan/PSVitaAlive/pull/85).
