# Reportes de PSVitaAlive — integración de PR #53 + #54

**Integrado en `main`:** 26 de octubre de 2026 mediante [PR #55](https://github.com/VegettoSan/PSVitaAlive/pull/55), commit `ea261c5f2e4e42f58d1dadcbfa01fef87c74ce8c`. **Fuente:** cambios de [PR #53](https://github.com/VegettoSan/PSVitaAlive/pull/53) y [PR #54](https://github.com/VegettoSan/PSVitaAlive/pull/54) reunidos en una rama combinada.

## Qué hace cada función

### A. Reportes automáticos y de instalación (PR #53)

Al terminar «Instalar todo», la pantalla muestra una lista de pasos con tipografía ampliada, detalles separados, estados y navegación touch/cruceta. Cada paso con error *reportable* muestra **Reportar** en su propia fila. SELECT permite reportar el paso seleccionado mediante el sistema de diagnóstico existente. Se incluyen archivo, motivo del error, identidad de la aplicación de la instalación e información contextual; se mantiene el comportamiento histórico de incluir logs correlacionados en estos reportes automáticos. Los estados de ZIP conservado o eliminado tras un error de extracción no ocultan el motivo original. No se crean reportes para tareas completadas ni para errores de espacio/conectividad considerados recuperables por el usuario.

El guardado automático de ZIP después de 10 segundos, los botones de conservación/borrado de ZIP, el reinicio por plugins y la continuación del wizard no se modifican.

### B. Reportes escritos y sugerencias (PR #54)

Desde **Reportar** del footer se abre una ventana con selector de **Problema / Sugerencia**, título obligatorio (máximo 64 caracteres) y descripción obligatoria (máximo 700 caracteres). Se reutiliza el teclado vita2d de búsqueda, con límite configurable y contador de caracteres; por defecto las búsquedas conservan el máximo previo. El texto se representa ajustado a la anchura del recuadro y permite scroll adaptativo. Controles táctiles y cruceta; pulsar Enviar es la única operación que realiza el POST. Al fallar el envío se conserva el borrador.

Los mensajes de esta ventana llegan a Discord con un **embed diferente**: marcador buscable `#feedback #user_report` o `#feedback #suggestion`, título, párrafo, versión real del cliente y fecha UTC obtenida del reloj de PS Vita. Este flujo **no lee ni adjunta logs**, ni utiliza automáticamente la aplicación seleccionada, el Title ID o el historial de descargas. Se comparten las limitaciones de frecuencia del sistema existente y se desactivan menciones automáticas de Discord.

Los reportes automáticos de descarga/instalación/extracción **continúan usando su formato anterior y sus diagnósticos**.

## Ficheros y arquitectura

- `Client PSVitaAlive/source/ui/full_catalog_screen.cpp` y `include/ui/full_catalog_screen.hpp`: ambas ventanas; navegación y estado de envío por paso y formulario.
- `Client PSVitaAlive/source/ui/search_keyboard.cpp` y `include/ui/search_keyboard.hpp`: contador y límites por campo.
- `Client PSVitaAlive/source/network/error_reporter.cpp` y `include/network/error_reporter.hpp`: flujo de feedback independiente del reporte diagnóstico.
- `Client PSVitaAlive/assets/lang/{en,es,de,fr,it,pt-BR,pt-PT,ru}.lang`: traducciones conjuntas.
- `.github/workflows/client-build.yml`: verificación de todas las cadenas añadidas.
- Guía pormenorizada de [PR #53](INSTALL_ALL_SUMMARY_REPORT_UI_20261026.md) y guía de [PR #54](MANUAL_FEEDBACK_FORM_20261026.md).

**La arquitectura oficial de catálogo no cambia.** Los datos fuente en `apps/`, `authors/` y `categories/` siguen generando los JSON finales mediante GitHub Actions.

## Seguridad, privacidad y límites

- Las descripciones del formulario son texto del usuario y no se escriben como contenido en los logs locales del cliente. La petición HTTP directa va a un webhook configurado en el cliente; el mantenedor debe tratar ese webhook como potencialmente recuperable de los VPK públicos.
- Los reportes automáticos siguen recogiendo sus logs de diagnóstico según la implementación existente; no deben confundirse con los reportes escritos sin logs.
- La marca de fecha depende de la hora configurada en la consola; en caso de reloj incorrecto, Discord puede mostrar una hora errónea.
- Existe cooldown entre envíos. No debe duplicarse un envío cuando el usuario pulsa varias veces.
- No prometer que todos los tipos de fallo se pueden reportar; los errores no reportables siguen filtrándose.

## Matriz de pruebas conjunta

1. «Instalar todo» sin errores: reporte claro, sin botones «Reportar».
2. Error individual de ZIP, VPK o plugin: botón dentro de la fila correcta; confirmar archivo, causa y logs relevantes en Discord.
3. Error de extracción ZIP y conservación automática tras diez segundos: el ZIP sigue disponible y se permite reportar el error original.
4. Reporte de formulario Problema y Sugerencia: verificar etiquetas, título, párrafo, versión, fecha y **ausencia total de logs**.
5. Límites de teclado: máximo 64/700 caracteres, contador visible, búsqueda antigua sin regresión.
6. Descripción larga: scroll táctil y por controles, texto que no invade encabezado ni botones.
7. Fallo de red de reporte escrito: borrador conservado; reintento posible; sin duplicados tras envío exitoso.
8. Probar ambos sistemas consecutivamente con el mismo cliente para detectar interferencias del hilo/cooldown.
9. Comprobar idioma español e inglés y que los otros seis paquetes tienen todas las claves.
10. Asegurar que PS TV puede navegar los dos diálogos sin pantalla táctil.

**Validación de hardware:** pendiente hasta que el mantenedor pruebe el VPK integrado y confirme el resultado. La compilación exitosa de GitHub Actions verifica el build, no sustituye las pruebas reales.
