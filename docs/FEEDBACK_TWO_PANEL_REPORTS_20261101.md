# PSVitaAlive — reportes y sugerencias en dos paneles (2026-11-01)

## Objetivo y compatibilidad

Se mejora **únicamente el formulario manual** que abre el botón `Reportar` en el pie del cliente, sin cambiar la descarga, los ZIP, los VPK, Install All ni la infraestructura de diagnósticos automáticos. La UI sigue usando vita2d y el teclado actual; la ruta Discord permanece separada (`#bug-reports` para Problemas, `#feature-requests` para Sugerencias). El cooldown compartido de 45 segundos y los límites de título (64) y descripción (700) siguen vigentes.

No se modifican `apps/`, `authors/`, `categories/` ni los catálogos autogenerados. No se añaden dependencias, servidores, bases de datos ni mensajes de diagnóstico a los reportes escritos.

## Diseño PS Vita (960 × 544)

La ventana adopta el patrón de **dos columnas de la ventana QR**, con una zona explicativa a la izquierda (~302 px) y el formulario a la derecha (~548 px), dentro de un modal de 900 × 512 px. El tamaño tipográfico de las explicaciones y los controles es comparable al del formulario y la ventana QR existentes: no se reduce la letra para forzar que todo quepa.

- Izquierda: orientación localizada, que cambia según `Problema` o `Sugerencia`. Para problemas, explica qué fallos del cliente corresponden al equipo y cuáles deben comunicarse al desarrollador original del Homebrew.
- Derecha: selector de tipo, bloque contextual de app **solo si hay vista de detalles abierta**, título, descripción y aviso de responsabilidad por fallos internos de los Homebrew (solo `Problema`).
- Dos áreas con *clipping* y scroll vertical independientes, cada una con indicador de posición. Se interpolan dos desplazamientos numéricos en pantalla para un movimiento suave. Se recalcula el ajuste de líneas al abrir el formulario o cambiar el tipo, no en cada fotograma.
- El texto de descripción también conserva su desplazamiento interior para mensajes largos. Los botones `Enviar` y `Cancelar` son permanentes, fuera de las zonas desplazables.
- Se mantiene el contraste y el tema visual del cliente; no se incorporan texturas o imágenes adicionales.

## Navegación

- **Táctil**: deslizar dentro de la columna izquierda desplaza exclusivamente la ayuda; deslizar dentro de la derecha desplaza el formulario. Cuando el desplazamiento comienza sobre la descripción y esta desborda, se mueve el texto de descripción. Tocar los tipos, título, descripción, enviar o cancelar funciona como antes.
- **Botones**: `TRIÁNGULO` alterna columna activa; `L/R` selecciona Problema/Sugerencia; `ARRIBA/ABAJO` mueve la selección en el formulario o desplaza la columna informativa; `IZQUIERDA/DERECHA` desplaza el formulario, cambia el tipo cuando ese control está enfocado o mueve el texto largo cuando la descripción está enfocada; `X` activa; `O` cierra (salvo envío en curso).
- El enfoque sobre título/descripción desplaza el formulario automáticamente para dejar visible el campo; se conserva el texto si el envío falla y se evita cambiar el catálogo mediante L/R mientras el formulario esté abierto.

## Identidad de aplicación y Discord

Al entrar en `openReportConfirm()`, si `state_.mode == UiMode::SPLIT_DETAIL` y `selectedIndex()` es válido, se toma una **copia inmutable** de `CatalogItem.name`, `id`, `titleId` y `version`. El usuario ve nombre, Title ID y versión en el formulario. Si está en FULL_CATALOG, no existe contexto asociado aunque haya una tarjeta resaltada; un reporte general sigue siendo general.

La petición `ManualFeedbackRequest.app` es opcional. Si hay nombre de aplicación, el título de hilo del foro tendrá el formato:

```text
🐛 Nombre del Homebrew: Título del usuario
💡 Nombre del Homebrew: Título del usuario
```

Si no hay ficha abierta, conservará `🐛 Título` o `💡 Título`. La función `feedbackForumThreadName` elimina controles, respeta los límites de 100 caracteres Unicode del foro y recorta sin dividir secuencias UTF-8. Se añaden campos de Discord `Application`, `App ID`, `Title ID`, `Version` **solo en contexto de app**. El resto del embed (mensaje, versión del cliente, fecha UTC, `allowed_mentions.parse=[]`) permanece intacto.

Los usuarios **pueden** reportar fallos de instalación o descarga vinculados a una app; el aviso sobre Ports no bloquea el envío. Los reportes escritos no recogen logs, lista de software instalado, dispositivos ni datos de otras apps.

## Archivos de código

- `Client PSVitaAlive/include/ui/full_catalog_screen.hpp`: estado y mensajes de ambos paneles y snapshot de app.
- `Client PSVitaAlive/source/ui/full_catalog_screen.cpp`: geometría, textos localizados, scroll, controles, envío de contexto.
- `Client PSVitaAlive/include/network/error_reporter.hpp`: campo opcional `ManualFeedbackAppContext`.
- `Client PSVitaAlive/source/network/error_reporter.cpp`: título y metadatos de Discord.
- `Client PSVitaAlive/include/network/feedback_forum.hpp` y `tests/test_feedback_forum.cpp`: título contextual, saneamiento UTF-8 y casos generales.
- `Client PSVitaAlive/assets/lang/{en,es,fr,de,it,pt-BR,pt-PT,ru}.lang`: textos en ocho idiomas.
- `.github/workflows/client-build.yml`: comprobación de las nuevas claves en todos los idiomas.

## Lista de verificación antes de publicar

1. Ejecutar el test de títulos de foros con g++ y compilar VitaSDK en GitHub Actions.
2. Abrir el formulario desde FULL_CATALOG y confirmar que **no** se adjuntan datos de la tarjeta resaltada.
3. Abrir una ficha Homebrew, pulsar `Reportar`, comprobar que aparecen nombre, Title ID y versión exactos; cambiar a Sugerencia y volver, conservando el mismo contexto.
4. Probar títulos largos y caracteres UTF-8; verificar títulos de los hilos `Nombre: Título` y el contenido del embed.
5. Desplazar cada columna por touch y botones; validar que no se mueve la otra columna, que el texto no se corta y que el formulario auto-desplaza al campo seleccionado.
6. Probar descripciones de hasta 700 caracteres, campos vacíos, aviso contextual, error de red, cooldown, cancelar y abrir el teclado.
7. Repetir en los ocho idiomas, en PS Vita real y Vita3K; revisar la legibilidad en 960 × 544 y el rendimiento.
8. Provocar un error automático de instalación/extracción y confirmar que continúa usando el canal de diagnósticos y sus logs, no los foros manuales.

**Alcance de la validación automática:** las pruebas y la compilación pueden detectar fallos de integración, pero **no sustituyen la comprobación táctil/visual en PS Vita real**.

## Seguridad operativa

Los URLs de webhook continúan compilados dentro del VPK: este cambio no incrementa privilegios, pero persiste el riesgo documentado de extracción y abuso de estos URLs. Si se filtra un webhook, rotarlo e incluir el nuevo en una versión del cliente.
