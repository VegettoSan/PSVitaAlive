# PSVitaAlive — Acciones contextuales, iconos y porcentaje real de borrado

**Base:** `efc53d4`. **Fecha:** 6 de noviembre de 2026.

## Alcance

Solamente el administrador de Game/Data Files de Homebrew del cliente PS Vita.
No cambia los JSON del catálogo, el instalador, los archivos Game Files ni la
arquitectura `apps/ + authors/ + categories/ -> GitHub Actions -> catálogos -> cliente/web`.

## Lista del administrador

- La lista muestra **tres** Homebrew por vista, en lugar de cinco, con tarjetas
  de 90 píxeles y el icono del catálogo (o la portada si no tiene icono).
- Las imágenes reutilizan `ImageCache` con namespace `app`; se solicitan
  únicamente las tres visibles y se decodifica como máximo una por frame.
  Sin icono o sin conexión se muestra el placeholder existente.
- Desplazamiento por cruceta, scroll táctil con acumulación de arrastre
  de 48 píxeles por fila (igual que el catálogo), toque para abrir y barra
  vertical de posición proporcional al número de aplicaciones.
- La lista no realiza un escaneo profundo adicional por dibujar iconos.

## Acciones según el contenido instalado

| APP presente | Game Files existentes y seguros | Acciones visibles |
|---|---|---|
| Sí | No | Borrar APP |
| No | Sí | Borrar Game Files |
| Sí | Sí | Borrar APP, Borrar Game Files, Borrar ambos |
| No | No | Ninguna |
| Sí | Existen pero están protegidos/no resueltos | Borrar APP |

La función pura compartida de `installed_data_policy.hpp` controla disponibilidad,
orden, posición y navegación. No se puede tocar ni enfocar una acción oculta.
Siguen vigentes las confirmaciones y la protección de la APP PSVitaAlive.

## Progreso al borrar Game Files

1. Antes de **cualquier** eliminación externa se valida la ruta, la existencia
   y la ausencia de solapamiento con **otra APP instalada**.
2. Se cuenta **completamente** cada archivo y directorio mediante el escáner
   recursivo existente, sin guardar nombres en RAM ni utilizar el tamaño en bytes
   como estimación. Durante la fase previa hay barra indeterminada y texto
   «Preparando borrado» localizado.
3. Si falla el conteo (I/O, ruta inválida, profundidad, límite de 200 000
   entradas por raíz), **no se inicia la eliminación**.
4. Cuando comienza el borrado, la barra avanza con
   `(archivos borrados + directorios borrados) / total de entradas × 100`.
   La UI muestra un porcentaje claro; los contadores internos permanecen
   disponibles para depuración. Se limita a 99 % durante el proceso para no
   anunciar finalización antes de verificar el resultado.
5. El VPK se desinstala con `scePromoterUtilityDeletePkg`. Esa etapa sigue
   siendo indeterminada: PromoterUtility no informa progreso porcentual fiable.
6. En «Borrar ambos» **primero** se realiza el preflight y conteo externo,
   **después** se desinstala APP, **luego** se borran los datos externos.
   `removeExternal` vuelve a validar la seguridad inmediatamente antes de
   borrar, por si hubo cambios entre etapas.
7. La caché de tamaños sigue siendo informativa, nunca una autorización
   destructiva. La medición por archivo conserva la política corregida en
   `efc53d4` para nombres internos como `temp`, `tai` y `sce_sys`.

## Validación

- Host: `tests/test_installed_data_policy.cpp` cubre menú contextual,
  navegación circular, posiciones de acciones, 0–100 % y reinicio de contadores.
- CI: compilar el cliente con VitaSDK y verificar las ocho traducciones.
- PS Vita física / Vita3K: comprobar lista de tres filas, carga perezosa de
  iconos, scroll lento y rápido, thumb, entrada táctil y botones físicos.
- Operaciones destructivas: usar Homebrew de prueba y **copia de seguridad**;
  probar Solo APP, Solo Game Files, ambos, datos compartidos con APP realmente
  instalada, archivo/carpeta profunda, directorio grande y fallo de I/O.
- El preconteo hace una lectura extra del árbol. Puede tardar con muchos archivos,
  pero evita mostrar porcentajes falsos.

**No declarar la prueba en hardware superada sin realizarla.**

## Vida de iconos GPU compartidos (revisión)

- No hay una segunda caché ni duplicados: el catálogo y el administrador
  resuelven cada URL de icono mediante `ImageCache::pathFor(url, "app")`.
- El administrador identifica **únicamente** los iconos que él decodifica
  por primera vez mientras está abierto. Los iconos ya residentes antes
  pertenecen al LRU global de apps y no cambian de propietario.
- Los registros de iconos decodificados por la ventana se recortan cuando
  el LRU descarta una textura, evitando acumulaciones si se recorren
  cientos de aplicaciones.
- Al salir, se calcula el conjunto de iconos requeridos por el catálogo
  visible que reaparecerá detrás del modal (incluida la ficha seleccionada).
  Se conservan las texturas compartidas y se programan para liberación
  diferida las que la ventana introdujo y **ya no son útiles al catálogo**.
- Se cancelan solicitudes de imágenes que solo interesaban a la ventana
  cerrada, sin borrar la caché de archivos descargados en `ux0:`.
- Se utiliza `scheduleTextureFree` y la cola GPU existente, nunca
  `vita2d_free_texture` mientras un frame podría seguir usando la textura.
- La política original del catálogo (`MAX_APP_TEXTURES=18` y LRU de
  iconos) permanece intacta; no se fuerza un borrado global ni se liberan
  texturas anteriores simplemente por cerrar este modal.
- El límite de decodificación de **una textura por frame** se verifica por
  clave, incluso cuando el LRU elimina una y agrega otra en el mismo frame.

La prueba host `tests/test_installed_data_policy.cpp` verifica retención
compartida y liberación exclusiva. En PS Vita/Vita3K hay que comprobar
visualmente abrir, recorrer, volver al catálogo, reabrir y cerrar el menú
repetidamente; la validación en hardware no se presume completada.
