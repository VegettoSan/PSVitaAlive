# PSVitaAlive — Comunidad Discord y acceso con QR

**Fecha:** 2026-10-27  
**Integración prevista:** `main`  
**Invitación oficial:** https://discord.gg/YnYSkFumFN

## Objetivo

Sustituir solo el botón **Noticias** del pie del catálogo del cliente por **Discord**. Al pulsarlo, se muestra la misma ventana QR que ya utiliza el cliente para abrir enlaces del catálogo en un teléfono o PC, con una invitación a la comunidad de PSVitaAlive.

## Cliente PS Vita

- `Client PSVitaAlive/source/ui/full_catalog_screen.cpp`: botón del pie con fondo Discord Blurple (`#5865F2`), borde claro (`#7983F5`) y texto blanco, sin depender del tema. Se conservan las coordenadas táctiles del botón anterior, a la izquierda de Report.
- `openDiscordQrModal()` reutiliza el generador `ensureWebQrPng()`, la textura `webQrTexture_` y el diseño de dos columnas; no se hacen llamadas HTTP para generar el QR. La columna izquierda muestra el QR y la URL. La derecha invita a unirse y explica cómo acceder.
- `webQrDiscord_` selecciona la marca morada y los textos propios de Discord en `drawWebQrOverlay()`. El QR de fichas (Open on phone / PC) sigue utilizando la guía y los colores normales.
- La ventana se dibuja en catálogo completo y vista de detalle. Se cierra por botón táctil o con X/O. La textura se libera con el mecanismo existente de liberación diferida.
- Cadenas `DISCORD_QR_*` añadidas a `en`, `es`, `fr`, `de`, `it`, `pt-PT`, `pt-BR`, `ru`.
- **No se elimina Noticias:** la comprobación de `news.txt` y los anuncios automáticos al iniciar siguen disponibles. Solo desaparece el acceso manual del pie.
- Reportes y solicitudes de datos enviados a Discord mediante webhooks se mantienen independientes.

## Repositorio y web

- El `README.md` de la raíz enlaza el Discord oficial.
- Los footers de `web/index.html`, `app.html`, `game.html`, `author.html`, `authors.html` y `category.html` enlazan la invitación, con `target="_blank"` y `rel="noopener noreferrer"`.
- `web/css/main.css` establece el color morado y texto blanco en todas las páginas. No se modifican los catálogos JSON ni los generadores.

## Verificaciones recomendadas

1. Probar el botón Discord en cada tema; comprobar que sigue morado y responde al toque.
2. Abrirlo desde lista y detalle: verificar QR, URL exacta `https://discord.gg/YnYSkFumFN` y apertura correcta desde el móvil.
3. Cerrar por toque y con X/O; confirmar que se devuelve el control al catálogo y que la textura se libera correctamente.
4. Abrir el QR de una aplicación y comprobar que mantiene los colores del tema y la guía de instalación.
5. Cambiar entre ocho idiomas y comprobar las traducciones, navegación y límites de texto.
6. Publicar una noticia con un nuevo `id` en `news.txt` y verificar que se muestra automáticamente, pero no al pulsar Discord.
7. Probar los enlaces de las seis páginas de la web tanto en móvil como en escritorio.
8. Compilar con VitaSDK y validar en PS Vita real / Vita3K antes de publicar una nueva release.

## Mantenimiento

La invitación puede cambiar o revocarse. Si cambia, actualizar la URL de `openDiscordQrModal()`, los enlaces de la web y de los README. El generador QR existente guarda el código por URL, por lo que generará uno nuevo si cambia. No depende de un servidor QR externo.

## Legibilidad del enlace — 2026-10-27

La URL visible bajo el QR utiliza **una sola presentación compartida** para las fichas de aplicaciones/juegos y el servidor de Discord:

- Escala de fuente incrementada de `0.65f` a `0.92f` (~42 % más), con espacio vertical de **25 píxeles** por línea.
- Panel de fondo oscuro fijo, texto blanco y borde del acento correspondiente: Discord morado o color elegido para el resto de QR.
- Hasta **tres líneas** visibles por vez, ajustadas al espacio entre el QR y el botón Cerrar. Los enlaces largos se parten según su ancho real en píxeles y siguen navegables con **izquierda/derecha**; el indicador se alinea a la derecha sin cubrir la etiqueta.
- El cálculo del ajuste de texto se hace al **abrir** cada modalidad QR y reutiliza las líneas en memoria durante el dibujo; no cambia los PNG, el caché ni el uso de red.
- Probar en una PS Vita real y Vita3K con enlaces cortos de Discord y enlaces largos de Homebrew, PS Vita, PSP y PS1, incluyendo cada estilo de fuente y los ocho idiomas.
