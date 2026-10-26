# PSVitaAlive — reportes escritos y sugerencias desde el footer

**Estado:** rama experimental `test/manual-feedback-form-20261026`. **No fusionar en `main` hasta prueba en PS Vita real.**

## Alcance

Se modifica **únicamente el botón Reportar del footer del cliente**, que antes ofrecía una confirmación de reporte manual vacío. Los reportes que se activan por errores de instalación, descarga, extracción ZIP, Install All o cualquier otra funcionalidad siguen usando el mecanismo automático anterior, con sus diagnósticos y bloqueos.

### Datos de la nueva ventana

- Tipo: **Problema** o **Sugerencia**.
- Título: obligatorio, máximo **64 caracteres**.
- Descripción: obligatoria, máximo **700 caracteres**.
- Se abre el teclado vita2d ya utilizado para buscar, tocando el campo o mediante controles. El teclado muestra un contador `actual/máximo` y deja de aceptar caracteres al llegar al límite.
- El formulario muestra ambos contadores y conserva el contenido si el usuario cancela una edición o el envío falla.
- Descripción envuelta en líneas dentro del recuadro y con barra de scroll cuando supera la altura visible. El texto no se dibuja fuera de su recuadro.
- El fondo, borde y acciones usan los colores del tema del cliente.
- La ventana muestra instrucciones explícitas: compartir un problema de PSVitaAlive o enviar una sugerencia; no se adjuntan logs ni datos del juego.

### Navegación

- Touch: seleccionar Problema/Sugerencia, tocar título o descripción, desplazar verticalmente la descripción, tocar Enviar/Cancelar.
- Cruceta o stick: arriba/abajo selecciona tipo, título, descripción, enviar o cancelar. X activa. Izquierda/derecha cambia tipo o desplaza el cuerpo cuando está seleccionado. O cierra si no hay envío en curso.
- No se abandona la ventana durante el envío. Después de enviar correctamente, se cierra y se limpia el borrador. Si el POST falla, se conserva para reintentar.
- El campo de búsqueda mantiene el límite de 128 caracteres anterior; otros usos existentes de `SearchKeyboard::open` también lo mantienen por defecto.

## Formato de Discord

`sendManualFeedback(ManualFeedbackRequest)` es una función independiente de `sendErrorReport`, que reutiliza el transporte `HttpClient::postJson` y el mismo webhook ya configurado en el cliente.

- Nombre emisor: **PSVitaAlive Feedback**.
- Mensaje buscable: `#feedback #user_report` o `#feedback #suggestion`.
- Embed: título marcado con icono, párrafo completo, color distinto por tipo, campos **Type**, **Client version** y **Submitted (UTC)**.
- Fecha: basada en el reloj de la PS Vita mediante `sceRtcGetCurrentClock` con UTC. Si no se puede obtener la hora se indica el tiempo de sesión en lugar de inventar fecha.
- La propiedad `allowed_mentions.parse: []` evita menciones automáticas por texto del usuario.
- **No llama a `buildLogBlock`, no abre archivos .log, no adjunta diagnósticos, ni añade aplicación seleccionada o Title ID de otra instalación.**
- Se comparte el cooldown de 45 segundos ya existente contra envíos repetidos.
- No se escribe el título ni el contenido libre del usuario en los logs locales del cliente.

**Privacidad y dependencia:** como en los reportes automáticos anteriores, el webhook incrustado en un VPK puede extraerse. A futuro conviene valorar medidas gratuitas antiabuso compatibles con la arquitectura basada en GitHub, sin añadir servidores propios. Este cambio no modifica las credenciales ni el canal usado por los errores automáticos.

## Archivos del repositorio

- `Client PSVitaAlive/include/ui/search_keyboard.hpp` y `source/ui/search_keyboard.cpp`: límites configurables, contador visible y borrado UTF-8 seguro.
- `Client PSVitaAlive/include/ui/full_catalog_screen.hpp` y `source/ui/full_catalog_screen.cpp`: ventana, edición, validación, vista previa y envío asíncrono.
- `Client PSVitaAlive/include/network/error_reporter.hpp` y `source/network/error_reporter.cpp`: petición manual y embed Discord sin logs.
- `Client PSVitaAlive/assets/lang/*.lang`: cadenas para ocho idiomas.
- `.github/workflows/client-build.yml`: comprobación de traducciones en CI.

## Plan de prueba en PS Vita

1. Abrir el botón Reportar del footer; distinguir Problema y Sugerencia.
2. Pulsar ambos campos, comprobar teclado, contraselección con O y que conserve el borrador.
3. Introducir 64 caracteres de título y comprobar que el siguiente se rechaza. Repetir con 700 para la descripción y verificar los contadores.
4. Escribir párrafo largo; comprobar que se envuelve y se recorre con touch y controles, sin desbordar la ventana ni ocultar los botones.
5. Probar formulario vacío; no debe hacer solicitudes a Discord.
6. Enviar un reporte y una sugerencia de prueba; confirmar que aparecen con distintivos correctos, título, texto, versión y fecha, **sin logs**.
7. Desconectar Internet antes de enviar: debe conservar el texto para reintentar.
8. Comprobar cooldown y que no se envía nada al entrar en la ventana o escribir.
9. Verificar en contraste distintos temas y en los ocho idiomas del cliente.
10. Provocar un error real de instalación/extracción y comprobar que sus botones de reporte automático y sus logs **no cambiaron**.
11. Validar búsqueda y teclado anterior con 128 caracteres; verificar que no se filtran controles mientras el teclado está abierto.

No modificar archivos generados de catálogo ni la arquitectura oficial `apps/ + authors/ + categories/ → Actions → catalog.json + authors.json + categories.json`.
