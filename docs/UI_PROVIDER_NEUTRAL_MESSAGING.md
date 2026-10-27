# PSVitaAlive — Mensajes de interfaz independientes del proveedor

**Alcance:** texto dirigido a usuarios, incluidos estados de progreso, errores,
avisos de conexión, ayuda de ajustes y notificaciones. Esta norma aplica
a todas las fuentes actuales y futuras: Archive.org, MediaFire, GitHub y otras.

## Regla de experiencia de usuario

Explicar **qué hace el cliente**, el resultado y la acción disponible. No
introducir nombres de hosts o proveedores en los textos genéricos.

| Situación | Texto visible | Detalle del log técnico |
| --- | --- | --- |
| Elección de fuente | Seleccionando un servidor de descarga... | Nodos y pruebas de Archive.org si corresponde |
| Validación | Verificando el servidor y el archivo... | URL, 206, ETag, rangos |
| Recuperación | Reanudando las partes verificadas... | Offsets y checkpoint |
| Falta de acceso | Esta fuente de descarga no responde... | Proveedor, código HTTP y causa técnica |
| Actualizaciones | Buscando actualizaciones... | Endpoint consultado y respuesta |

No afirmar que **todos** los proveedores están caídos cuando el error
solo afecta a un enlace o servicio. La recuperación conserva sus
comprobaciones de tamaño/ETag/Content-Range y sus alternativas.

## Excepciones importantes

No ocultar la identidad de un autor, la atribución obligatoria, el nombre
de un plugin, el origen de un enlace seleccionado, ni la procedencia de
software al mostrar información específica del proyecto. Esta norma
se refiere a **mensajes de estado genéricos**, no a metadatos, créditos,
licencias o fuentes explícitas.

Los identificadores heredados (`ARCHIVE_WARNING_*` y
`INSTALL_ERR_MEDIAFIRE_*`) se mantienen por compatibilidad interna,
pero el **valor mostrado** ya no menciona proveedor. En futuras funciones,
preferir claves nuevas basadas en la función (`HTTP_STAGE_*`),
no en el servicio.

## Desarrollo / traducciones

- Guardar mensajes locales en `Client PSVitaAlive/assets/lang/*.lang`.
- Mantener las claves estables para todos los idiomas; el gestor usa
  inglés como respaldo si falta una traducción.
- Prohibir cadenas en inglés codificadas directamente en la vista para
  estados que deban traducirse.
- Mantener nombres exactos de servicios y URLs solamente en registros
  `diagnostics::log`, resolución HTTP y la procedencia explícita.
- `tests/test_provider_neutral_ui_copy.py` comprueba en CI que ningún
  pack vuelva a añadir nombres comerciales a estados, avisos y errores
  genéricos.
- La interfaz no debe ganar un mensaje nuevo por cada proveedor incorporado.

## Seguridad y alcance experimental

La modificación es exclusivamente de textos y presentación de fallos;
no cambia rutas de descarga, clasificación HTTP 206, resolución de
proveedores, protección OLED, bloqueo durante instalaciones, actualización
automática ni catálogos. No modificar `main` sin validación de la rama.
