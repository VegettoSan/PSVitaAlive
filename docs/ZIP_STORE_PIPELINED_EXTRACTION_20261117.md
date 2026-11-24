# PSVitaAlive — Extracción ZIP STORE con lectura/escritura en paralelo

**Fecha:** 17 de noviembre de 2026  
**Rama:** `feature/zip-store-pipelined-extraction-20261117`  
**Estado:** implementación experimental; se requiere compilación VitaSDK y validación en PS Vita física antes de integrar en `main`.

## Objetivo y alcance

Los paquetes de Game/Data Files publicados por PSVitaAlive utilizan normalmente ZIP **STORE** (método 0): el contenido no se comprime. El tamaño de los archivos extraídos es aproximadamente el del ZIP menos sus cabeceras. Durante la instalación coexisten el ZIP original (uno o cuatro rangos) y los archivos de salida hasta finalizar con éxito.

Este cambio incorpora una ruta opcional para **archivos internos individuales de al menos 8 MiB**, con método STORE y tamaño descomprimido conocido. Todo lo demás mantiene la lectura y escritura secuenciales existente.

## Diseño

- **Hilo principal del instalador:** conserva la propiedad exclusiva de `libzip`. Lee con `zip_fread`, valida entradas, comprueba cancelación y actualiza el progreso.
- **Nuevo hilo VitaSDK de escritura:** únicamente llama a `sceIoWrite` sobre el descriptor de salida ya abierto. No accede a `zip_t`, `zip_file_t`, la UI ni al catálogo.
- **Cola circular:** tres bloques de 256 KiB (768 KiB), con dos semáforos de VitaSDK. El productor espera si no quedan bloques libres; el consumidor espera si no hay datos. Se reserva una señal adicional para cerrar el trabajador sin bloquearlo.
- **Errores y cancelación:** se detiene la producción, se libera al productor si estaba bloqueado, se espera a que termine el trabajador y, **solo entonces**, se cierran `zip_file_t` y `sceIo`. Se mantienen las reglas existentes para archivos nuevos, sustituciones, ZIP guardados y staging de rutas profundas.
- **Progreso:** informa los bytes confirmados por `sceIoWrite`, no los simplemente leídos de libzip.
- **Fallback:** si no se pueden crear las estructuras o el hilo, se conserva la extracción secuencial actual.
- **Observabilidad:** `[ZipPerf]` registra `pipeline_files` y `pipeline_bytes`, sin registrar cada bloque individual.

## Compatibilidad

No se modifican `apps/`, `authors/`, `categories/`, los JSON generados, los enlaces de descarga, `Install All`, ZIP64, las lecturas de dos/cuatro rangos, libzip/CRC, el formato de los VPK ni las comprobaciones de rutas. Los datos comprimidos DEFLATE (método 8), los ZIP pequeños y los métodos inusuales usan el camino anterior.

La concurrencia de lectura/escritura puede mejorar el rendimiento, pero también puede causar contención en tarjetas lentas; **no se presume una mejora de velocidad hasta medirla en hardware real**. Tampoco resuelve un ZIP corrupto, un límite por archivo del sistema de archivos ni la falta de espacio.

## Pruebas

1. Prueba de política en CI: `tests/test_zip_write_pipeline_policy.cpp` para STORE grande, STORE pequeño, tamaño desconocido, DEFLATE y métodos incompatibles.
2. Compilación de VitaSDK y generación de VPK en GitHub Actions.
3. Prueba en Vita real con ZIP STORE pequeño, ZIP STORE con un archivo grande, ZIP STORE con muchos archivos, y BT4 en cuatro rangos.
4. Cancelación a mitad de archivo y escritura forzada fallida (dispositivo lleno); verificar que no hay cuelgues y que el ZIP recuperable no desaparece.
5. ZIP64 superior a 4 GiB, archivo de salida grande, sobrescrituras existentes, directorios profundos y errores deliberados de CRC.
6. Comparar tiempo real y registros `[ZipPerf]` con el build previo en la **misma tarjeta**. Sin resultados de esta matriz, mantener PR en borrador.

## Referencia de diseño

Se estudiaron las ideas de procesamiento lector/escritor de VitaForge, pero esta implementación es original, en C++, y emplea las primitivas de VitaSDK y libzip ya presentes en PSVitaAlive. No se reutilizó código de Rust ni se añadió su biblioteca ZIP.
