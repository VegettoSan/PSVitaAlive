# PSVitaAlive — Corregir falsos positivos de carpetas compartidas y mediciones incompletas

Fecha: 6 de noviembre de 2026. Reporte del usuario desde PS Vita física.

## Diagnóstico confirmado en main (commit 9354625)

1. `InstalledDataManager::scan()` comparaba **todas** las raíces de **todas** las aplicaciones del catálogo, aunque la otra aplicación no estuviera instalada, y marcaba `shared` y `blocked` en ambas. Ejemplo real del inventario canónico: `ux0:data/ctr` figura tanto para CTR: High Octane como para Cut the Rope Classic. No prueba dos instalaciones.
2. El bloqueo `blocked` de una coincidencia afectaba a la posibilidad de borrar datos y a la presentación de las aplicaciones. **La medición en sí no consultaba el indicador shared**: algunos tamaños quedaban "Sin calcular" por causas separadas.
3. `safeComponent()` reutilizaba `validInstalledRelativePath()` para cada archivo y carpeta **interna**. Esa rutina prohíbe nombres como `temp`, `tmp`, `tai`, `sce_sys`, `app` y `psvitaalive` como primer componente de una ruta raíz. Pero son nombres legítimos **dentro de una carpeta de juego**. Devolvía `false` en lecturas legítimas y podía bloquear borrados recursivos.
4. La validación de **toda la ruta** en cada paso recursivo también rechazaba rutas profundas de archivos legítimos por el límite de componentes de la ruta raíz, aunque el directorio base estuviera validado.
5. La medición automática permitía solo 8,5 segundos por aplicación y no reintentaba el título incompleto durante la misma sesión. Un juego grande podía quedar sin medición incluso cuando sus datos eran accesibles.
6. La bandera `blocked` se activaba por falta de tiempo o lectura parcial, confundiendo **incapacidad de medir bytes** con **ruta insegura**.

## Cambios

- Eliminar la comprobación global de solapamiento de las **declaraciones del catálogo** como fuente de `shared/blocked` y quitar la etiqueta «Compartida» del administrador.
- Mantener `installed_paths` como única fuente oficial de rutas externas. Nunca inferir propiedad del directorio por los datos de la caché.
- Autorizar la medición sin mirar `blocked` ni comparaciones de catálogo. Los intentos incompletos conservan «Sin calcular» y no se registran como completos en la caché.
- Validar la **raíz** mediante `safeExternalDataPath`. Una vez validada la raíz, cada nombre de archivo/directorio descendiente se comprueba con `validInstalledChildName` (sin `/`, `\\`, `:`, controles ni `.`/`..`). No se aplican las restricciones del primer nivel a los nombres internos.
- Aumentar el tiempo de análisis cooperativo automático por aplicación a 25 segundos, y el de medición explícita a 35 segundos. Se conserva la cancelación segura al cerrar y la UI fluida. El sistema de archivos podría bloquear una llamada individual más tiempo.
- Si el análisis profundo es incompleto, ya **no se marca la aplicación protegida** ni se deshabilita borrar datos por el solo hecho de no conocer su tamaño. El borrado revalida las rutas y recorre entradas con comprobaciones propias.
- Para borrar datos externos, **mantener una prevención mínima** ante una segunda APP que **sí está instalada** y declara la misma ruta o una ruta antecesora/descendiente. Se comprueba `ux0:app/<TITLE_ID>/sce_sys/param.sfo` justo antes de borrar. La coincidencia con una APP no instalada no bloquea; la coincidencia con una APP verdaderamente instalada **sí**. Protege contra un borrado conocido de datos de otra instalación.
- Mantener bloqueos de ruta insegura, datos sin `installed_paths` fiable, título del cliente y directivas de confirmación antes de borrar. No modificar el catálogo generado, instalador, archivos de Game Files ni estados `Verified/Legacy/Archive`.

## Limitación relevante

Si dos proyectos del catálogo mencionan una misma ruta pero uno no está instalado como VPK, no se puede demostrar solo con metadatos cuál creó los archivos. La confirmación de borrado y el aviso sobre partidas siguen siendo indispensables. El cliente no puede detectar todas las modificaciones hechas por VitaShell ni dar garantías de propiedad histórica del directorio.

## Pruebas

- Pruebas puras: ruta `ctr` con otra APP no instalada no constituye colisión activa; con otra APP instalada sí. Límites de componentes, rutas root protegidas y nombres internos legales/ilegales.
- Compilación VitaSDK: `installed_data_manager.cpp`, `installed_data_screen.inc`, pruebas de políticas y VPK.
- Consola: comprobar que no aparecen falsas carpetas «compartidas», que `temp`/ `sce_sys` anidados se cuentan, que los títulos grandes ya terminan la medición o muestran fallo parcial sin deshabilitar borrado.
- Probar desinstalación únicamente de un Homebrew temporal con copia en VitaShell; mantener la prevención si **dos aplicaciones realmente instaladas** señalan una ruta coincidente.

**No cambiar manualmente** `catalog.json`, `authors.json` ni `categories.json`.
