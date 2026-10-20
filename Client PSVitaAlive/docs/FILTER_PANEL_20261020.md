# Panel de filtros del catálogo — prueba manual

**Rama:** `feature/catalog-filter-panel-20261020`. No incluye optimización de RAM, nuevos catálogos ni cambios al instalador.

## Comportamiento implementado

- El botón superior **Filtros** aparece únicamente cuando `UiMode::FULL_CATALOG` puede recibir entradas. Se oculta en detalles, transiciones, ajustes, carga, instalaciones, protecciones y diálogos de reporte / confirmación.
- **□** abre el panel solo en cuadrícula completa. □ en la ficha de detalle conserva la función previa de solicitar archivos. □ en errores / reportes mantiene el manejo anterior; no abre filtros.
- **X** aplica una opción y cierra; **O** cierra sin cambiarla; **□** también cierra la ventana; flechas o joystick recorren el menú omitiendo encabezados.
- La ventana admite pulsación táctil y deslizamiento; los toques en el fondo quedan consumidos, y tocar fuera la cierra.
- Homebrew ofrece Todos, Game/Data Files combinados, solo Game Files, solo Data Files y categorías que ya figuran en el catálogo.
- PS Vita / PSP mantienen «Con DLC» y ofrecen categorías. PS1 ofrece categorías cuando se encuentran.
- Un filtro activo se combina con la búsqueda de texto. O en la cuadrícula limpia filtro y búsqueda, igual que antes. Cambiar de catálogo los reinicia.
- `CatalogItem::category` se obtiene del catálogo ya cargado. No se solicita `categories.json` ni se modifican los JSON oficiales.
- Los nombres de categoría conocidos están traducidos en los ocho archivos `.lang` empaquetados; las categorías futuras sin traducción muestran su ID legible.

## Pruebas antes de fusionar a main

1. Compilar en GitHub Actions el cliente VitaSDK; verificar que no existan errores de C++ ni fallos de empaquetado de `.lang`.
2. En Vita3K y una Vita real: comprobar □ y táctil en Full Catalog, incluyendo abrir/cerrar repetidamente.
3. Deslizar el listado al final con pantalla táctil y con cruceta / joystick; elegir una categoría y verificar resultados.
4. Combinar búsqueda de texto con Game Files, Data Files, DLC y categoría; usar O para limpiar todo; volver a abrir panel.
5. Entrar en detalle: botón visual ausente. Usar □ en solicitud de archivos; volver al catálogo y comprobar botón visible.
6. Abrir News, Ajustes, temas, confirmación de reporte, errores de instalación y ZIP recovery: el panel no debe poder abrirse ni con □ ni tocando la posición anterior.
7. Cambiar entre Homebrew, PS Vita, PSP y PS1: comprobar que no se filtra el catálogo nuevo por estado anterior.
8. Validar ocho idiomas, textos largos, glifos rusos, fuentes y tamaños de texto en 960×544.
9. Comprobar que abrir filtros no inicia descargas, no hace peticiones a categories.json ni cambia la instalación o el reporte.

## Fuera de alcance

No se implementó la conversión de `std::vector<CatalogItem> items_` a un vector de índices: esa optimización de memoria requiere pruebas dedicadas de carga, instalación y navegación.
