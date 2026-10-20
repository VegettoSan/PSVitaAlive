# Panel de filtros del catálogo — prueba manual

**Rama:** `feature/catalog-filter-panel-20261020`. No incluye optimización de RAM, nuevos catálogos ni cambios al instalador.

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

## Pruebas antes de fusionar a main

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

Se ajustaron el ancho del botón, el icono □ (dibujado sin depender de la fuente), el marquee para idiomas largos, el tamaño del texto del panel y la alineación del área táctil con el logotipo activo. Debe verificarse la legibilidad del nuevo panel en Vita3K antes de fusionar.

## Adaptación de categorías según catálogo

Los juegos comerciales llevan comúnmente `category_id: "games"`, por lo que ese campo no sirve para diferenciar géneros. El parser ya extrae la primera entrada de `subcategory_ids` a `CatalogItem::subcategory`. El menú diferencia filtros `Category` (Homebrew) y `Genre` (PS Vita, PSP, PS1). Las opciones viven en tablas `static constexpr FilterOption` con IDs y claves de traducción literales; **no recorre el catálogo ni crea `std::vector`, `std::string` o listas temporales al abrir**. La etiqueta se obtiene directamente de la localización y la categoría seleccionada se guarda como puntero a la tabla estática.

## Listas congeladas (instantánea 2026-10-20)

- **Homebrew (8)**: customization, emulators, games, media, networking, plugins, ports, utilities.
- **PS Vita (15)**: action, adventure, arcade, fighting, horror, music, other, platformer, puzzle, racing, rpg, shooter, simulation, sports, strategy.
- **PSP (14)**: action, adventure, arcade, fighting, horror, music, other, platformer, puzzle, racing, rpg, shooter, sports, strategy.
- **PS1 (12)**: action, adventure, arcade, fighting, horror, other, platformer, puzzle, racing, rpg, shooter, sports.

Un género futuro no aparece automáticamente en la lista; sus juegos seguirán disponibles en «Mostrar todo». Los cambios en listas fijas se harán más adelante en el código, sin tocar JSON generados.
