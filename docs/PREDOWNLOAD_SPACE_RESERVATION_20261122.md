# PSVitaAlive — Reserva de espacio ANTES de descargar (Install All e individual)

**Fecha de implementación:** 22 de noviembre de 2026  
**Origen de implementación:** `feature/zip-store-pipelined-extraction-20261117` (PR #85, investigación inicial).  
**Integración estable:** [PR #110](https://github.com/VegettoSan/PSVitaAlive/pull/110), fusionado en `main` el 24/11/2026 mediante `1d99d034abb26b21f982e02a144c6bdd207fc085`.  
**Validación:** pruebas host y compilación VitaSDK #646, validación del catálogo #3328 aprobadas; **pendiente** matriz física específica de poco espacio, distintos enlaces y cancelación.  
**Historial:** [documentación completa de la integración](PR85_STABLE_INTEGRATION_20261124.md). El extractor ZIP experimental de PR #85 **NO** fue fusionado.

## Causa del problema

La política anterior para un ZIP se basaba en el tamaño **comprimido** con solo 5% adicional y 32 MiB. En los Game/Data ZIP en modo **STORE** del proyecto, al extraer siguen existiendo el ZIP/rangos descargados y los archivos ya extraídos. La reserva antes de descargar era insuficiente; `ZipExtractor` detectaba después la falta de espacio y detenía una instalación que ya había consumido tiempo/red.

Además, `Install All` reservaba un porcentaje insuficiente para los ZIP STORE. Si cualquier enlace carecía de tamaño, se anulaba la comprobación conjunta y se delegaba cada paso por separado, incluso **después de haber instalado pasos anteriores**.

## Nuevo comportamiento

### Descargas individuales

- Al pulsar **Download/Mirror, Game Files, Data Files, Plugin, VPK, PSP/PS1 o una descarga BGDL** que termine en `InstallController::requestInstall`, se comprueba `ux0:` *antes de encolar/crear el trabajo de red*.
- Para ZIP de extracción con tamaño declarado se reserva **2,25 × tamaño descargable + 64 MiB**. Esto incluye una copia descargada, una extraída de tamaño semejante (STORE) y 25% de holgura + una reserva fija.
- Para VPK/PKG de instalación directa y PKG de BGDL se mantiene un presupuesto de **2,1 × tamaño**; para plugins se mantiene archivo + 4 MiB.
- Si falla la consulta de espacio, el trabajo se **bloquea** y muestra un error traducido; antes se omitía la comprobación.
- Sin tamaño de catálogo, se usa una reserva mínima de 256 MiB para ZIP, 64 MiB para paquete y 16 MiB para plugin. Esta reserva NO garantiza la capacidad final: `DownloadManager` utiliza el tamaño HTTP 200/206 real antes de escribir el primer byte, si el servidor lo declara.
- Si el contenido real es mayor que el anunciado, el control de cabeceras antes del cuerpo protege de empezar esa transferencia sin capacidad suficiente.

### Install All

- El wizard construye primero la lista exacta de enlaces seleccionados (no suma espejos o alternativas no elegidos).
- **Una sola comprobación conjunta**, justo antes de iniciar el primer trabajo: presupuestos VPK + ZIP de todos los Game/Data Files seleccionados + plugins aprobados + **128 MiB**.
- Si algún enlace carece de tamaño, **no se pierde** la parte conocida del presupuesto. Se suma reserva mínima según tipo (ZIP 256 MiB, paquete 64 MiB, plugin 16 MiB), además de la validación real de cabeceras HTTP antes de cada cuerpo.
- Si no hay espacio suficiente, o no se puede leer `ux0:`, se muestra `SpaceWarning` **sin iniciar ninguna descarga**.
- Al aprobar la capacidad total, cada paso posterior de `Install All` reutiliza esta aprobación. Se evita **volver a realizar la estimación inicial** con un `freeBytes` que ya refleja archivos descargados/extraídos de etapas anteriores.
- La aprobación NO anula comprobaciones reales del tamaño remoto, errores de escritura, reserva mínima durante transferencia o integridad ZIP.

### Protección final que debe mantenerse

No es seguro suprimir por completo las comprobaciones durante la escritura: otra aplicación podría ocupar espacio o un ZIP DEFLATE podría expandirse mucho más que el tamaño descargado. Por eso se conserva el control de espacio libre mínimo del descargador y el análisis exacto de tamaño/crecimiento de `ZipExtractor` justo antes de extraer. Son **protecciones ante hechos reales**, no una segunda estimación optimista basada en el ZIP. No se elimina la recuperación de ZIP ni la cancelación.

## Matriz de prueba

1. **Minecraft Modern Legacy:** ZIP STORE declarado ~1,181 GB; no debe permitir descarga cuando solo cabe el ZIP y no su extracción; con espacio suficiente debe completar sin error tardío por la doble ocupación prevista.
2. **BT4:** ZIP STORE declarado 5.905.580.032 bytes; presupuestar ZIP + extraído + margen antes de descargar.
3. **Install All:** VPK + Game Files, dos ZIP seleccionados, plugins, espejo alternativo no seleccionado, una entrada desconocida, y errores de consulta ux0.
4. **Individual:** Download, Mirror, Game Files, Data Files, Plugin y BGDL; comprobar modal de espacio antes de descargar.
5. **Guardas:** servidor con tamaño incorrecto, descargas pausadas/reanudadas, ZIP DEFLATE de alta expansión, extracción recuperada desde ZIP ya guardado y tarjeta llena durante instalación.
6. Comparar el `session.log` y el estado de descarga: al denegar en el botón no debe existir un trabajo nuevo ni una transferencia HTTP.

## Límites y compatibilidad

Una estimación a partir del tamaño descargado no puede garantizar un archivo comprimido arbitrario. Sin `Content-Length` ni tamaño en el catálogo tampoco puede conocerse de antemano cuánto ocupará una fuente externa; se mantiene una reserva mínima y la comprobación en tiempo de ejecución. Los archivos en `apps/` y catálogos generados siguen intactos. Los paquetes ZIP STORE del proyecto cuentan con el presupuesto conservador apropiado. No se requiere un servicio ni biblioteca adicional.
