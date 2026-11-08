# PSVitaAlive — espacio unificado de Apps + Game/Data Files (2026-11-07)

## Alcance y navegación

Se conserva el flujo **Administrar espacio → menú general UX0 → Aplicaciones y Game Files (primera fila) → Homebrew → acciones**. La limpieza de temporales, ZIP conservados, catálogos e imágenes conserva sus reglas. **Limpiar todo** sigue limitado a temporales y ZIP; nunca borra Homebrew, rutas externas ni datos personales.

La lista usa `title_id` para comprobar la presencia de paquetes (`ux0:app/<title_id>/sce_sys/param.sfo`) y `links[].installed_paths` validados para Game/Data Files externos. Los datos externos pueden existir aunque se haya quitado el VPK; también es posible que exista el VPK sin rutas externas. Una carpeta presente no certifica que su instalación sea completa.

## Índice único persistente

Se reutiliza `ux0:data/psvitaalive/cache/installed_directory_index.tsv` con formato **PSVA_DIR_INDEX_V1**, sin migración destructiva. Se añaden registros con ruta exacta `ux0:app/<title_id>` para tamaños de paquetes; los registros anteriores `ux0:data/<ruta>` siguen siendo válidos. El validador permite solamente esa ruta de paquete idéntica al Title ID del registro. **El índice es exclusivamente informativo: jamás autoriza desinstalaciones o borrado de carpetas.**

Cada registro guarda Title ID, ruta, huella de fecha de modificación de la raíz, indicador de medición completa y bytes. La huella es una señal insuficiente para asegurar que las subcarpetas no cambiaron; por tanto, las cantidades recuperadas son **últimas mediciones**, marcadas con `*`. Las mediciones incompletas no se persisten como completas. El formato mantiene límite de 1024 entradas y 128 KiB; si se supera, el guardado falla sin transformar un valor parcial en exacto.

## Secuencia y memoria

1. Al abrir el administrador principal se ejecuta el escaneo original de archivos propios del cliente.
2. El mismo trabajador consulta los Title IDs y las raíces conocidas con `sceIoGetstat`, leyendo el índice. **No recorre recursivamente todas las aplicaciones y Game Files al abrir.**
3. Construye un resumen de bytes medidos, contando los Game/Data Files externos por ruta física una única vez y los paquetes por Title ID una única vez.
4. El primer renglón de Aplicaciones y Game Files presenta el total medido o «Sin calcular», y el total superior incluye ese resultado. Si hay elementos presentes sin medición o la comprobación fue parcial, el encabezado indica «Parcial»; si hay mediciones históricas, indica «Última medición».
5. Al entrar al inspector permanece la lógica **uno por uno**, con trabajador y presupuesto de tiempo; se mide cada Homebrew (raíces externas y árbol del paquete) secuencialmente. La acción manual «Medir tamaño» continúa disponible. El tamaño del VPK comprimido NO se usa como tamaño de instalación.
6. Al volver al menú principal se actualiza el resumen desde el snapshot ya disponible **en memoria**, sin ejecutar otro escaneo profundo.
7. Una desinstalación verificada deja de mostrar el paquete y sus bytes; si quedaron datos externos continúan visibles. Instalaciones, actualizaciones y eliminaciones de datos realizadas por PSVitaAlive invalidan los registros afectados. Cambios externos requieren nueva comprobación de existencia; para conocer tamaños exactos tras cambios internos efectuados por VitaShell se necesita una medición nueva.

## Seguridad

- La medición recursiva de un paquete se permite solamente para `ux0:app/<title_id>` verificado y **en modo de lectura**. La rutina destructiva `eraseTree` continúa restringida a raíces permitidas bajo `ux0:data/`.
- La desinstalación del VPK utiliza exclusivamente `scePromoterUtilityDeletePkg` y comprueba el resultado; nunca utiliza borrado recursivo de `ux0:app/`.
- Las colisiones de rutas, metadatos ambiguos, datos compartidos, Title ID protegido y procesos de instalación activos conservan sus barreras anteriores.
- Los identificadores internos de acciones permanecen 0 (App), 1 (Game Files) y 2 (ambos). Únicamente se reordena la navegación visible a **ambos → App → Game Files**, sin cambiar sus significados ni confirmaciones.
- Los Game Files que realmente residen dentro del paquete se contabilizan en el tamaño de App, nunca también como datos externos.
- No se modifica el esquema oficial `apps/ + authors/ + categories/ → Actions → catalog.json + authors.json + categories.json`. Los catálogos generados no se editan.

## Pruebas

CI ejecuta el test de políticas de desinstalación y navegación, el test del índice V1 (incluye filas mixtas de paquetes y datos y rechazo de rutas maliciosas), y el build VitaSDK con VPK. **CI no sustituye la prueba en consola física.**

Casos imprescindibles: App+datos; solo App; solo datos; ninguna ruta; reinstalar App; borrar App desde VitaShell y regresar; instalar datos; carpeta parcialmente extraída; ruta compartida; medición cancelada; carpeta grande y tiempo límite; cierre con medición automática activa; regreso inmediato al resumen; fallo de desinstalación y fallo parcial; comparar RAM y latencia con la versión anterior.

El total superior indica el **espacio identificado por PSVitaAlive**, no todos los bytes ocupados en la partición UX0. Si existen tamaños sin calcular, el total conocido es parcial; si procede del caché es histórico. Las partidas guardadas pueden estar en rutas distintas a las incluidas en Game Files, y el usuario debe realizar copia antes de eliminar.
