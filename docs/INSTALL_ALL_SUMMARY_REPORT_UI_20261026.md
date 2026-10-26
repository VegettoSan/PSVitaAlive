# PSVitaAlive — reporte final de Install All: legibilidad y reportes por paso

**Estado:** integrado en `main` el 26 de octubre de 2026 mediante [PR #55](https://github.com/VegettoSan/PSVitaAlive/pull/55), que incorpora los PR #53 y #54. El build del conjunto pasó en VitaSDK; sigue recomendándose la validación funcional en PS Vita real.

## Objetivo

El **reporte que aparece al finalizar «Instalar todo»** debe ser fácil de leer en la pantalla 960×544 de PS Vita: menos información amontonada, letras más grandes, espacio entre resultados y un botón **«Reportar» dentro de la propia fila del archivo/paso que falló**. No se abre una ventana de reporte adicional; el envío usa la infraestructura existente.

## Diseño

- Ventana 900×508 con título, contadores de completados y problemas separados.
- Resultados en filas de aproximadamente 75 px, hasta tres visibles simultáneamente; selección con arriba/abajo y desplazamiento táctil vertical para más elementos.
- Cada fila distingue el nombre del paso, su estado y un detalle más legible. El texto largo se recorta **en pantalla mediante clipping**, sin eliminar el motivo completo del error que se necesita para diagnosticar.
- Los pasos que pueden notificarse muestran un botón rojizo **«Reportar»** dentro de su misma fila. Durante el envío aparece «Enviando...» y, tras éxito, «Enviado».
- El botón queda asociado a ese paso; no depende de la selección actual del catálogo.
- Las acciones existentes sobre un ZIP conservado **no se eliminan**: conservar/reconfirmar y eliminar con confirmación siguen al pie del reporte. Continuar/Reiniciar también se conserva.

## Reglas de reporte

Solo son candidatos los pasos con estado **Failed, Kept o Deleted** cuando contienen una razón de error reportable. Estos dos últimos estados indican que una extracción ZIP falló, pero luego se resolvió la conservación o eliminación del archivo; **el motivo original se guarda por separado** en `failureReason` para que no lo sustituya la ruta del ZIP ni «archivo eliminado».

No se ofrece reportar para pasos Completed, Verified, AlreadyInstalled, Skipped, Cancelled ni NotAttempted. Tampoco se invita a reportar errores de espacio insuficiente, pérdida de Internet o casos bloqueados por la política existente `isNonReportableInstallError`. Esta política se vuelve a verificar cuando se llama al remitente.

El reporte enviado utiliza el `ErrorReportRequest` existente y conserva:

- `app.name`, `app.titleId`, `app.version`: datos de la **aplicación de Install All** guardados al iniciar la cola, no de un elemento seleccionado posteriormente.
- `fileName`: archivo del paso que falló, cuando el instalador lo proporcionó; de lo contrario, nombre del paso.
- `title`: «Install All step failed», procesado como `InstallFailed` por el mecanismo existente.
- `context`: índice del paso, nombre y motivo de error original. **No se agregan enlaces de descarga ni rutas de ZIP conservados como nuevos campos de este reporte**.

El envío requiere una pulsación deliberada. No se produce automáticamente durante la instalación y no se repite un reporte enviado con éxito en la misma pantalla de resultados. Si falla la solicitud, el usuario puede intentar nuevamente, mientras el trabajador no esté ocupado.

## Controles

- **Táctil:** pulsar «Reportar» dentro de la fila fallida; desplazar verticalmente para recorrer resultados.
- **Arriba/abajo:** seleccionar paso. **SELECT:** reportar el paso seleccionado, solo si es reportable.
- **Triángulo:** confirmar conservación del ZIP seleccionado; **Cuadrado:** solicitar eliminación de ZIP conservado, manteniendo la segunda confirmación existente.
- **X:** continuar o reiniciar según las necesidades del instalador; **O:** cerrar cuando el instalador lo permite.

El sistema utiliza el trabajador de reportes ya existente, sin introducir dependencias o servidores adicionales.

## Cambios previstos en el repositorio

- `Client PSVitaAlive/source/ui/full_catalog_screen.cpp`: resultados, hitboxes, controles y envío contextual.
- `Client PSVitaAlive/include/ui/full_catalog_screen.hpp`: fallo/archivo por resultado, generación de sesión y estado del reporte.
- `Client PSVitaAlive/assets/lang/{en,es,de,fr,it,pt-BR,pt-PT,ru}.lang`: cuatro nuevas cadenas.
- `.github/workflows/client-build.yml`: comprobación de traducciones.

## Pruebas recomendadas en PS Vita

1. Install All sin fallos: no debe aparecer «Reportar».
2. Install All con falla VPK, plugin, Game Files o Data Files: el botón pertenece solo al paso fallido.
3. Extracción ZIP fallida y guardada automáticamente después de 10 segundos: mostrar «Reportar» y mantener las opciones del ZIP, sin borrarlo accidentalmente.
4. Tras eliminar voluntariamente un ZIP del informe, conservar la posibilidad de reportar el fallo original.
5. Comprobar envío, «Enviando...» y «Enviado», y verificar un único reporte por paso exitosamente notificado.
6. Fallo de red al enviar: notificación de error y opción de reintentar.
7. Error por poco espacio o sin Internet: no ofrecer un reporte bloqueado por la política.
8. Recorrer resultados largos por touch y botones; probar los ocho idiomas y temas de color.
9. Cuando Install All instala un plugin y exige reinicio, mantener el botón de reinicio y no abandonar accidentalmente el reporte.
10. Confirmar que no se alteraron el guardado automático de ZIP ni la arquitectura del catálogo.

**Integración realizada:** el código ya forma parte de `main` por solicitud del mantenedor; mantener las pruebas de regresión indicadas para futuras versiones.
