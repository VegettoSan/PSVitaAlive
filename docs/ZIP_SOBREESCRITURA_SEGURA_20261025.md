# ZIP/VPK: estimación de espacio y sobrescritura segura — 25-10-2026

**Estado:** comportamiento presente en `Client PSVitaAlive/source/archive/zip_extractor.cpp` al revisar `main` (commit `cb43ae4`). Este documento describe código existente; **no implementa cambios**.

## Objetivo y elección de diseño

Antes de extraer, `ZipExtractor` examina las entradas, las rutas de destino,
el tamaño descomprimido y el espacio de la partición de destino. La estimación
estándar usa bytes nuevos más **32 MiB + 5 %** de margen. Si no hay espacio
para todos los bytes, se hace una pasada adicional para calcular el
crecimiento **neto** cuando se reemplazan archivos ya existentes.

En destinos normales, al sobrescribir un archivo se utiliza
`sceIoOpen(..., SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, ...)`. El tamaño
anterior de ese archivo puede recuperarse al truncarlo; por ello, el coste
adicional no siempre equivale a otra copia completa del directorio.

## Seguridad y límites

- La deducción del tamaño antiguo ocurre **solo cuando el espacio está
  ajustado**; se evitan `stat` repetidos en casos normales por rendimiento.
- Solo se descuenta, como máximo, el menor entre el tamaño antiguo y el
  tamaño descomprimido de la entrada nueva. Se evita contar una ruta dos veces.
- Nunca se da crédito a una entrada que pueda colisionar con los **archivos
  fuente** del ZIP; la validación posterior prohíbe sobrescribir el propio
  archivo o sus rangos.
- Los **descendientes profundos** se extraen a staging independiente: no se
  descuenta un archivo antiguo como reutilizable si podría acabar en staging.
  No se fusionan ramas profundas ya existentes y no vacías.
- El extractor sigue validando rutas, compresión soportada, tamaños de
  entrada y errores de lectura/cierre.
- Un archivo nuevo se crea de forma exclusiva cuando es posible. Si una
  extracción falla, **solo se borra automáticamente el destino probado como
  nuevo mediante creación exclusiva**.
- Para un archivo que ya existía y fue truncado, no hay copia de seguridad ni
  rollback garantizado: si falla la extracción, puede quedar un fichero
  **incompleto** y el error lo advierte. Es preferible a eliminar sin permiso
  el archivo del usuario, pero exige nueva extracción o recuperación manual.
- La reubicación `sceIoRename` de ramas profundas solo se declara correcta
  después de completar la extracción y validar la rama.
- No existe garantía universal de espacio cuando los ZIP tienen tamaños
  comprimidos anómalos o el servidor omite el tamaño: el chequeo completo
  descomprimido tiene lugar en la fase de extracción.

## Relación con Install All y ZIP guardados

1. `FullCatalogScreen` determina si todos los enlaces **de la cola**
   tienen `size`.
2. En modo conjunto valida la suma seleccionada + reserva; en modo individual
   `InstallController` valida cada enlace con su tamaño conocido.
3. `HttpClient` comunica tamaño real de respuesta 200/206 a
   `DownloadManager` antes del cuerpo. En descarga 2/4 rangos, se usa el
   sondeo 206 que ya existía.
4. `ZipExtractor`, ya con el ZIP descargado, estima **crecimiento
   descomprimido** y decide si puede extraer.
5. Si una extracción ZIP de datos completa falla, la recuperación manual
   conserva el archivo o los rangos crudos bajo `manual/<id>/`.
6. Al reextraer un ZIP guardado se comprueba de nuevo el espacio real; no
   necesita una nueva descarga ni debe concatenar los rangos en RAM.

Las decisiones de espacio están detalladas en
[INSTALL_ALL_SPACE_PREFLIGHT_20261025.md](INSTALL_ALL_SPACE_PREFLIGHT_20261025.md).
La persistencia y acciones de recuperación están en
[SAVED_ZIP_SEGMENTS_20261025.md](SAVED_ZIP_SEGMENTS_20261025.md).
La limitación de directorios profundos está en
[ZIP_RECOVERY_AND_RANGE_ASSEMBLY_20261018.md](ZIP_RECOVERY_AND_RANGE_ASSEMBLY_20261018.md).

## Pruebas de regresión recomendadas

- Primera instalación con suficiente espacio y otra con espacio insuficiente.
- Reinstalar ZIP reemplazando archivos existentes grandes, con poco espacio
  pero suficiente para el crecimiento **neto**.
- Cortar/cancelar a mitad de la sobrescritura: confirmar que el original
  incompleto **no se borra**, que existe aviso y que la recuperación no
  sobreescribe silenciosamente su propia fuente.
- Forzar fallo al crear un archivo nuevo: eliminar solo ese archivo probado
  como nuevo, sin borrar otros archivos del usuario.
- Caso ZIP de origen en el mismo árbol destino; colisión de nombres con
  diferencias de mayúsculas/minúsculas.
- >2 GiB, ZIP64, 2/4 rangos, destino `ux0:`/otra unidad, ramas profundas no
  vacías, cancelación, fallo de `sceIoRename` y posterior reintento.
- Medir uso temporal de almacenamiento y RAM en Vita real, no solo en Vita3K.

Las pruebas unitarias de políticas y la compilación VitaSDK son útiles, pero
**no sustituyen** las pruebas de extracción física con ZIPs reales.
