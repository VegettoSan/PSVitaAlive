# ZIP con miles de archivos pequeños: bloqueo de UI (2026-11-13)

## Evidencia física recibida

Cliente 02.18, Homebrew Minecraft Modern Legacy `LCEM00001`, ZIP de Game Files, cuatro rangos HTTP 206.

- La VPK se descargó, extrajo y promocionó correctamente.
- El ZIP de Game Files descargó 1.206.133.472 bytes; las longitudes de los cuatro segmentos se verificaron y se conservaron para recuperación.
- El ZIP se abrió correctamente y comenzó a extraerse en `ux0:data/`, con siete ramas temporales para rutas profundas.
- La prevalidación de directorios tardó decenas de segundos.
- Se observan `entry_read_complete` hasta la entrada 5.376, pero no hay `ZipPerf` final ni `InstallDispatcher` de éxito para el ZIP.
- La velocidad aproximada pasó de 10,75 entradas/s en 1.024–4.096 a 0,83 entradas/s en 5.120–5.376.
- La UI registró `worst_us` de 22,9 s y hasta 53,4 s durante esa instalación.
- El usuario tuvo que apagar forzosamente la consola; no se registró un error de libzip ni se demostró un deadlock.

Las rutas con muchos archivos pequeños, en particular `Minecraft/Common/Modern/sound/bee`, son candidatas a presión de metadatos en el sistema de archivos. No hay aún una medición individual de cada llamada de E/S para afirmar que esta sea la causa única.

## Cambios propuestos en la rama

1. `ZipWorkPacer` cede 1 ms de CPU al completar 8 entradas o 1 MiB, lo que ocurra primero. Antes solo se cedía por cada MiB, de modo que miles de archivos mínimos podían acumular apertura/escritura/cierre sin pausa.
2. La prevalidación de directorios admite cancelación y pausas cada 16 directorios preparados; se registra su duración.
3. En ZIPs de al menos 512 entradas, `[ZipIO]` reporta cada 15 s contadores **acumulados**: entradas/bytes completados, tiempo total, tiempo de `zip_fopen_index`, creación/comprobación de destino, `zip_fread`, `sceIoWrite`, `zip_fclose`, `sceIoClose` y peor apertura. Una apertura de 2 s o más también genera un aviso acotado (como máximo cada 15 s).
4. La UI muestra `ZIP files: N/M` durante extracción de Game/Data ZIP, además del progreso por bytes. No se envían rutas largas a la UI.
5. No cambia el comportamiento de libzip, validación de paths, CRC, staging profundo, sustitución de archivos, borrado de creados al fallar ni recuperación de ZIP descargado.

No se presenta esto como solución definitivamente confirmada: un único `sceIoOpen`/escritura que bloquee el kernel durante muchos segundos no se puede interrumpir simplemente haciendo `sceKernelDelayThread` *después*.

## Validaciones antes de integrar a main

- [x] Prueba C++11 de política de pausas (archivos diminutos, entrada grande, mezcla).
- [ ] Build VitaSDK y empaquetado VPK de prueba.
- [ ] Instalación física en Vita: ZIP pequeño + ZIP mixto con rutas profundas, sin regresión en VPK.
- [ ] Repetir Minecraft con ZIP conservado; comprobar que UI responde a botones y la cancelación, sin apagado forzado.
- [ ] Comparar en la **misma** consola / tarjeta / ZIP los intervalos de entradas/s y pausas UI antes/después.
- [ ] Comparar deltas entre snapshots `[ZipIO]`, porque son acumulados. Si `file_open_ms` crece principalmente, investigar el coste del directorio destino; si `write_ms` o `zip_read_ms` dominan, investigar el medio de almacenamiento y libzip; si predomina el tiempo no contabilizado, analizar prevalidación, construcción de paths, logging y scheduling.
- [ ] Confirmar que archivos originales, partidas y árbol `ux0:data/Minecraft` permanecen íntegros; no borrar ese directorio como parte de la prueba.
- [ ] Copiar `ux0:data/psvitaalive/logs/session.log` y `install.log` después de la prueba y **antes** de reiniciar PSVitaAlive, porque `install.log` se reinicia al arrancar.

No modificar manualmente los catálogos generados ni fusionar esta rama antes de validar en Vita física.
