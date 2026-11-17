# PSVitaAlive — Minecraft Modern Legacy: diagnóstico y optimización de extracción

**Fecha:** 17 de noviembre de 2026  
**Rama:** `feature/zip-store-pipelined-extraction-20261117`  
**Estado:** prueba experimental, requiere CI completa y verificación en una PS Vita física.

## Caso registrado

Aplicación: **Minecraft Modern Legacy**, ID `minecraft-modern-legacy`, Title ID `LCEM00001`. La fuente oficial en `apps/minecraft-modern-legacy.json` contiene un enlace Game Files a `Game Files for PSVA.zip` en Archive.org, tamaño declarado **1.181.116.006 bytes**, destino `ux0:data/`, ruta instalada `Minecraft`.

Se ha reportado públicamente que la instalación aparenta congelarse y el indicador desciende a unos 100 KiB/s. Sin un `session.log` de esa instalación no es posible clasificar la causa como corrupción ZIP, saturación de I/O, insuficiencia de espacio, ruta profunda, problema de red o UI. Fuente de referencia: https://www.reddit.com/r/VitaPiracy/comments/1xsuwy8/ps_vita_alive_issue/

Este informe **no prueba que todos los ZIP o todos los dispositivos se comporten así**. No se ha descargado ni abierto el ZIP real en esta sesión para enumerar sus entradas.

## Cambios incorporados para diagnóstico y rendimiento

1. **Reutilización de carpetas ya preparadas:** el preflight recopila, ordena, valida y crea los directorios del ZIP. Si un archivo posterior tiene un padre incluido en esa lista, el extractor ya no hace una comprobación redundante `sceIoGetstat`/creación. Se conserva la ruta original cuando el padre no está verificado.
2. **Protección para staging:** los archivos de ramas profundas usan directorios temporales; nunca se presume que su ruta temporal está preparada únicamente por la lista de destinos finales.
3. **Progreso de preparación:** al comenzar la inspección se envía `Preparing ZIP metadata` y durante la inspección de rutas, cada 256 entradas, `Checking ZIP paths`. Los bytes escritos permanecen en cero hasta que empieza la extracción; no se inventan porcentajes.
4. **Tiempos agregados:** `[ZipTiming] total_us=... preflight_us=... extract_and_cleanup_us=...`, más el tiempo de preflight en el resumen de directorios. No se emiten logs por bloque.
5. **Pipeline STORE grande:** el otro cambio de esta rama activa un hilo lector y otro escritor para entradas STORE de al menos 8 MiB. No acelerará automáticamente una colección formada principalmente por entradas pequeñas.

No se modifican `apps/` ni los JSON generados, rutas de extracción del catálogo, el CRC de libzip, límites de memoria, recuperación de ZIP o tratamiento de ZIP64/partes segmentadas.

## Plan de verificación

1. CI: pruebas de `zip_preflight_directory_policy`, `zip_write_pipeline_policy` y build VitaSDK completo con VELF + VPK.
2. En Vita real: instalar Minecraft Modern Legacy desde Game Files, medir inicio y progreso, extraer y comprobar que el port encuentra `ux0:data/Minecraft`.
3. Si se ralentiza: conservar `session.log` y distinguir `[ZipTiming] preflight_us` del tiempo de extracción; comprobar `[ZipPerf]` y `[ZipDiag]` para conocer entrada, método, tamaño y errores de I/O.
4. Probar cancelación durante preflight, durante `sceIoWrite` y recuperación de ZIP. Repetir con BT4 (>4 GiB ZIP) y un VPK común.
5. Medir frente a la versión `02.18` en el mismo dispositivo y tarjeta. Evitar atribuir diferencias a código si los archivos o el estado del almacenamiento no son idénticos.

**No integrar en main hasta validar la CI y los casos físicos.**
