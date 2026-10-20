# Panel de filtros del catálogo — implementación y mantenimiento

**Estado:** integrado en `main` mediante [PR #40](https://github.com/VegettoSan/PSVitaAlive/pull/40) el **2026-10-20**; commit de integración [`473a10f`](https://github.com/VegettoSan/PSVitaAlive/commit/473a10fe6a07c70247daa3d8da4eae715dcf7bd3). Rama de origen: `feature/catalog-filter-panel-20261020`.

**Alcance:** interfaz nativa del catálogo completo, filtros estáticos y traducciones. No incluye optimización adicional de RAM, nuevos catálogos, cambios al instalador ni modificación de JSON fuente o catálogos generados.

## Estado de validación e integración

- **GitHub Actions — Build PS Vita Client:** éxito para la revisión fusionada, incluyendo compilación con imagen oficial de VitaSDK y subida de VPK smoke-build ([ejecución #155](https://github.com/VegettoSan/PSVitaAlive/actions/runs/39455491958)).
- **GitHub Actions — Validate and Generate Local Catalog:** éxito, incluyendo generación local y validación de catálogos ([ejecución #2340](https://github.com/VegettoSan/PSVitaAlive/actions/runs/39455491807)).
- **Revisión funcional:** el responsable del proyecto confirmó haber validado la información del PR #40 y solicitó integrarlo a `main` el 2026-10-20. Las pruebas de Vita3K/hardware real no quedan certificadas por GitHub Actions; conservar la lista inferior como regresión en versiones futuras.
- **Fusión:** inicialmente en draft, después marcado ready for review y fusionado con método `merge`, preservando los commits del PR.


## Comportamiento implementado

- El botón superior **□ Filtros** es más ancho que antes y anima el texto mediante marquee cuando un idioma no cabe. Aparece únicamente cuando `UiMode::FULL_CATALOG` puede recibir entradas. Se oculta en detalles, transiciones, ajustes, carga, instalaciones, protecciones y diálogos de reporte / confirmación.
- **□** abre el panel solo en cuadrícula completa. □ en la ficha de detalle conserva la función previa de solicitar archivos. □ en errores / reportes mantiene el manejo anterior; no abre filtros.
- **X** aplica una opción y cierra; **O** cierra sin cambiarla; **□** también cierra la ventana; flechas o joystick recorren el menú omitiendo encabezados.
- La ventana ahora usa tipografía ampliada y cinco filas visibles, con desplazamiento táctil o físico para ver el resto. Los toques en el fondo quedan consumidos, y tocar fuera la cierra.
- Homebrew ofrece **Mostrar todo**, **Game Files o Data Files** (única opción combinada) y ocho categorías **fijas en el cliente**.
- PS Vita y PSP mantienen «Con DLC». PS Vita, PSP y PS1 usan **listas fijas de géneros**: 15, 14 y 12, respectivamente, según los `subcategory_ids` encontrados en sus catálogos actuales.
- Los géneros están definidos por separado en cada pestaña, mediante tablas `static constexpr`; para añadir un género habrá que actualizar y recompilar el cliente.
- Un filtro activo se combina con la búsqueda de texto. O en la cuadrícula limpia filtro y búsqueda, igual que antes. Cambiar de catálogo los reinicia.
- `CatalogItem::category` se obtiene del catálogo ya cargado. No se solicita `categories.json` ni se modifican los JSON oficiales.
- Cada opción fija incluye su clave de traducción; están presentes en los ocho archivos `.lang`.

## Lista de regresión para nuevas compilaciones

Las siguientes comprobaciones se conservan como guía de QA para versiones posteriores (no como tareas pendientes de fusión del PR #40). Los pasos de Vita3K/consola requieren verificación manual específica de cada nuevo build.

1. Compilar en GitHub Actions el cliente VitaSDK; verificar que no existan errores de C++ ni fallos de empaquetado de `.lang`.
2. En Vita3K y una Vita real: comprobar □ y táctil en Full Catalog, incluyendo abrir/cerrar repetidamente.
3. Deslizar el listado al final con pantalla táctil y con cruceta / joystick; elegir una categoría y verificar resultados.
4. Combinar búsqueda de texto con **Game Files o Data Files**, DLC y categoría; usar O para limpiar todo; volver a abrir panel.
5. Entrar en detalle: botón visual ausente. Usar □ en solicitud de archivos; volver al catálogo y comprobar botón visible.
6. Abrir News, Ajustes, temas, confirmación de reporte, errores de instalación y ZIP recovery: el panel no debe poder abrirse ni con □ ni tocando la posición anterior.
7. Cambiar entre Homebrew, PS Vita, PSP y PS1: comprobar que no se filtra el catálogo nuevo por estado anterior.
8. Validar ocho idiomas, géneros y categorías largos, glifos rusos, fuentes y tamaños de texto en 960×544.
9. Comprobar que abrir filtros no inicia descargas, no hace peticiones a categories.json ni cambia la instalación o el reporte.
10. Abrir filtros en cada pestaña: Homebrew debe ofrecer 8 categorías, PS Vita 15 géneros, PSP 14 y PS1 12; comprobar que la búsqueda combinada sigue filtrando bien.

## Fuera de alcance

No se implementó la conversión de `std::vector<CatalogItem> items_` a un vector de índices: esa optimización de memoria requiere pruebas de carga, instalación y navegación. **Las opciones del menú no realizan asignaciones dinámicas**, pero al aplicar un filtro el cliente todavía reconstruye la lista filtrada usando la estrategia existente.

## Revisión posterior a pruebas en Vita3K (2026-10-20)

Se ajustaron el ancho del botón, el icono □ (dibujado sin depender de la fuente), el marquee para idiomas largos, el tamaño del texto del panel y la alineación del área táctil con el logotipo activo. La legibilidad debe revisarse en cada nueva compilación, especialmente si se cambian fuentes, temas o textos.

## Adaptación de categorías según catálogo

Los juegos comerciales llevan comúnmente `category_id: "games"`, por lo que ese campo no sirve para diferenciar géneros. El parser ya extrae la primera entrada de `subcategory_ids` a `CatalogItem::subcategory`. El menú diferencia filtros `Category` (Homebrew) y `Genre` (PS Vita, PSP, PS1). Las opciones viven en tablas `static constexpr FilterOption` con IDs y claves de traducción literales; **no recorre el catálogo ni crea `std::vector`, `std::string` o listas temporales al abrir**. La etiqueta se obtiene directamente de la localización y la categoría seleccionada se guarda como puntero a la tabla estática.

## Listas congeladas (instantánea 2026-10-20)

- **Homebrew (8)**: customization, emulators, games, media, networking, plugins, ports, utilities.
- **PS Vita (15)**: action, adventure, arcade, fighting, horror, music, other, platformer, puzzle, racing, rpg, shooter, simulation, sports, strategy.
- **PSP (14)**: action, adventure, arcade, fighting, horror, music, other, platformer, puzzle, racing, rpg, shooter, sports, strategy.
- **PS1 (12)**: action, adventure, arcade, fighting, horror, other, platformer, puzzle, racing, rpg, shooter, sports.

Un género futuro no aparece automáticamente en la lista; sus juegos seguirán disponibles en «Mostrar todo». Los cambios en listas fijas se harán más adelante en el código, sin tocar JSON generados.

## Archivos relacionados

- `include/ui/full_catalog_screen.hpp`: estado del filtro, `ContentFilter`, `FilterOption`, selección de la vista y funciones del panel.
- `source/ui/full_catalog_screen.cpp`: tablas fijas, filtrado, dibujo, posición táctil, navegación y restricciones de modo.
- `assets/lang/{en,es,fr,de,it,pt-PT,pt-BR,ru}.lang`: traducciones `FILTERS_*`, `FILTER_CATEGORY_*` y `FILTER_GENRE_*`.
- Esta guía: `docs/FILTER_PANEL_20261020.md` (rutas relativas a `Client PSVitaAlive/`).

## Cómo mantener o ampliar los filtros

1. **Examinar las fuentes oficiales**, sin editar catálogos agregados a mano: para Homebrew, revisar `categories/` y `apps/`; para juegos, revisar las subcategorías de los JSON del catálogo correspondiente.
2. **Identificar el ID exacto** que usa el cliente: `CatalogItem::category` en Homebrew y `CatalogItem::subcategory` (primera entrada de `subcategory_ids`) en juegos.
3. **Actualizar la tabla fija correspondiente** de `FilterOption` en `source/ui/full_catalog_screen.cpp`. No convertir la tabla a listas generadas recorriendo `allItems_` al abrir el modal: el objetivo es mantener las opciones con memoria estable.
4. **Añadir la clave de traducción** en los ocho `.lang`; no introducir texto visible sin su traducción y fallback existente.
5. **Recompilar con VitaSDK** y comprobar controles, toque, desplazamiento, filtros combinados con búsqueda y cuatro pestañas. Usar la lista de regresión anterior.
6. **Verificar la compatibilidad** al cambiar de catálogo y al abrir detalle, ajustes, errores y diálogos. Si una nueva categoría/género no está en la tabla fija, seguirá navegable mediante «Mostrar todo» hasta publicar un nuevo cliente.

### Decisiones de arquitectura y límites

- No consumir `categories.json` desde el panel. Los metadatos permanecen en los catálogos existentes; se evita añadir solicitudes remotas al abrir los filtros.
- No modificar `catalog.json`, `authors.json` ni `categories.json` manualmente: pertenecen al flujo `apps/ + authors/ + categories/ → GitHub Actions → catálogos publicados`. Los tres catálogos comerciales siguen independientes.
- Las tablas `static constexpr` evitan crear listas de opciones dinámicas al abrir el modal, **pero no significan que el filtrado sea de costo cero**: `items_` sigue reconstruyéndose al filtrar o buscar.
- No mezclar la optimización pendiente de `items_` con mantenimiento rutinario del panel sin pruebas de RAM, navegación e instalación en consola real.
- Los filtros de Homebrew se basan en **categorías**; los de juegos en **géneros/subcategorías**. No sustituir los géneros comerciales por `category_id: "games"`.
- Mantener la geometría compartida entre dibujo e hitboxes táctiles, los límites de `UiMode::FULL_CATALOG` y los usos anteriores de □ en otras pantallas.

## Referencias

- [PR #40 — historial de revisión y código fusionado](https://github.com/VegettoSan/PSVitaAlive/pull/40)
- [Build PS Vita Client — éxito en CI](https://github.com/VegettoSan/PSVitaAlive/actions/runs/39455491958)
- [Validate and Generate Local Catalog — éxito en CI](https://github.com/VegettoSan/PSVitaAlive/actions/runs/39455491807)
