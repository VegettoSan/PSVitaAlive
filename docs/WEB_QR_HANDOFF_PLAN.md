# PSVitaAlive — QR / Web handoff desde el cliente

> **Estado actual (auditado el 25-10-2026):** la función **ya está
> implementada en `main`**: `webPageUrl()`, `makeWebQrGuide()`,
> `ensureWebQrPng()` y modal `FullCatalogScreen`. Los tests de URL y
> guía dinámica se ejecutan en el workflow **Build PS Vita Client**, que
> compiló el commit `cb43ae4`. **No implica** validación del nuevo QR
> en PS Vita real en todas las redes/dispositivos.
>
> **Nota histórica:** el texto que sigue nació como plan de una rama previa.
> Para mantenerlo útil, las recomendaciones formuladas como «debería» no
> sustituyen el comportamiento verificable del código de `main`.
>
> **Objetivo:** permitir que, desde la ficha de una aplicación o juego en el cliente de PS Vita, el usuario abra un modal con un código QR y la URL de la ficha equivalente en la web de PSVitaAlive para continuar la descarga desde un teléfono o PC.
>
> **Regla principal:** esta función es una capa de cliente. No modifica la arquitectura oficial del catálogo, no añade campos nuevos a los JSON públicos y no introduce una nueva fuente de verdad.

---

## Estado técnico contrastado con `main` (25-10-2026)

- URL de Homebrew: `app.html?title_id=<title_id>`; de juegos:
  `game.html?catalog=psvita|psp|ps1&id=<id>`, con validación y
  percent-encoding de los IDs. Código:
  `Client PSVitaAlive/source/ui/web_page_link.cpp`.
- La guía de instrucciones se genera dinámicamente a partir de los enlaces
  del elemento, diferenciando VPK, Game Files, Data Files, ISO, PBP, PKG
  y rutas de extracción utilizables. Código:
  `Client PSVitaAlive/source/ui/web_qr_guide.cpp`.
- El QR se genera **localmente** con `qrcodegen` y PNG, sin solicitar una
  imagen a un servicio externo. Su PNG se guarda bajo
  `ux0:data/psvitaalive/cache/images/v3/<bucket>/qr_<hash>.png`;
  participa del mantenimiento normal del caché de imágenes.
  Código: `Client PSVitaAlive/source/ui/qr_code.cpp`.
- La UI abre y cierra el modal sobre la ficha, con URL visible y guía;
  no cambia JSON públicos ni descarga binarios desde el teléfono.
- Pruebas de host: `tests/test_web_page_link.cpp`,
  `tests/test_web_qr_guide.cpp`; compilación VitaSDK:
  [workflow aprobado](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40250988724).

### Pruebas manuales aún útiles

Comprobar en Vita real escaneo de QR con móviles, selección táctil y botones,
Unicode/IDs escapados, juegos de las tres plataformas, datos sin enlace
descargable, borrado del caché y conectividad de GitHub Pages.

---

## 1. Motivación

Algunos usuarios tienen descargas lentas desde la propia PS Vita y prefieren descargar archivos grandes desde un PC o teléfono.

La web pública ya permite abrir la ficha individual del contenido. La propuesta consiste en ofrecer esa misma URL directamente desde el cliente mediante:

- un botón contextual en la ficha;
- un código QR generado localmente;
- la URL escrita debajo del QR;
- un modal sencillo con opción de cerrar.

El cliente no debe descargar el QR desde un servicio externo.

---

## 2. Arquitectura que no debe cambiar

La arquitectura oficial de Homebrew sigue siendo:

```text
apps/ + authors/ + categories/
        ↓
GitHub Actions
        ↓
catalog.json + authors.json + categories.json
        ↓
Cliente PS Vita + Web
```

Los catálogos generados no se editan manualmente.

Esta función **no debe**:

- añadir `qr_url`, `web_url` ni campos equivalentes a `apps/*.json`;
- añadir datos QR a `catalog.json`;
- modificar `catalog_parser.cpp` para convertir el QR en parte del esquema público;
- generar o almacenar miles de QR en el repositorio;
- depender de un servicio externo de generación de QR;
- crear una segunda base de datos o sistema de caché independiente si puede reutilizarse el existente.

---

## 3. Datos ya disponibles

El cliente ya dispone en memoria de los datos necesarios para construir la URL web:

- `CatalogType`;
- `CatalogItem::id`;
- `CatalogItem::titleId`;
- nombre y resto de metadatos de la ficha.

Por tanto, la URL debe derivarse dinámicamente.

---

## 4. URLs de destino

Base web actual:

```text
https://vegettosan.github.io/PSVitaAlive/
```

### Homebrew

La web identifica el Homebrew por `title_id`.

Formato:

```text
https://vegettosan.github.io/PSVitaAlive/app.html?title_id=<TITLE_ID>
```

Ejemplo:

```text
https://vegettosan.github.io/PSVitaAlive/app.html?title_id=VITAQUAKE
```

### Juegos PS Vita

Formato previsto:

```text
https://vegettosan.github.io/PSVitaAlive/game.html?catalog=psvita&id=<ID>
```

### Juegos PSP

```text
https://vegettosan.github.io/PSVitaAlive/game.html?catalog=psp&id=<ID>
```

### Juegos PS1

```text
https://vegettosan.github.io/PSVitaAlive/game.html?catalog=ps1&id=<ID>
```

La construcción de estas URLs debe estar centralizada en un helper y no repetida dentro de la UI.

Propuesta:

```text
include/ui/web_page_link.hpp
source/ui/web_page_link.cpp
```

Responsabilidad:

```cpp
std::string webPageUrl(CatalogType catalog, const CatalogItem& item);
```

---

## 5. Generación del QR

El QR debe generarse **localmente en la PS Vita** únicamente cuando el usuario abra la función.

No debe precalcularse para todo el catálogo.

Flujo:

```text
usuario abre ficha
      ↓
pulsa Web / QR
      ↓
construir URL web
      ↓
buscar QR cacheado
   ├─ existe → reutilizar
   └─ no existe
          ↓
      generar QR local
          ↓
      guardar PNG
          ↓
      mostrar modal
```

### Biblioteca recomendada

Usar una implementación QR pequeña y autocontenida en C.

Candidato previsto:

```text
Nayuki QR Code Generator (C)
```

Integración propuesta:

```text
third_party/qrcodegen/qrcodegen.c
third_party/qrcodegen/qrcodegen.h

include/ui/qr_code.hpp
source/ui/qr_code.cpp
```

La biblioteca QR se encarga de generar la matriz de módulos.

`qr_code.cpp` se encarga de:

- recibir la URL;
- generar la matriz;
- crear una imagen PNG;
- mantener quiet zone;
- escribir el archivo en el caché;
- informar éxito/error a la UI.

Debe verificarse la licencia exacta de la versión incorporada en el momento de implementación y conservar su aviso correspondiente.

---

## 6. Render del QR

El QR debe priorizar legibilidad y facilidad de escaneo, no la paleta visual del tema.

Recomendaciones:

- módulos negros;
- fondo blanco;
- quiet zone blanca;
- sin suavizado;
- escalado por múltiplos enteros;
- tamaño visual aproximado de 220–240 px;
- no usar verde sobre negro para la matriz QR.

El marco, textos, título y botones del modal sí deben respetar el tema activo de PSVitaAlive.

---

## 7. Integración con Image Cache v3

La intención es reutilizar el sistema de caché existente.

Actualmente el caché de imágenes está en:

```text
ux0:data/psvitaalive/cache/images/v3/
```

El QR debe persistir en disco para reutilizarse si el usuario vuelve a abrir la misma ficha.

### Comportamiento deseado

```text
primera solicitud
   ↓
generar QR
   ↓
guardar PNG
   ↓
mostrar

segunda solicitud
   ↓
PNG existe
   ↓
reutilizar
```

Si el mantenimiento del caché elimina el archivo:

```text
QR eliminado por limpieza
      ↓
usuario lo solicita otra vez
      ↓
cache miss
      ↓
se regenera localmente
```

No requiere red para reconstruirse.

### Namespace

Se propone introducir un namespace interno:

```text
qr
```

El nombre/identidad del archivo debe depender de la URL final o de una identidad estable equivalente, evitando colisiones.

Ejemplo conceptual:

```text
qr_<hash-url>.png
```

### Importante

El Image Cache v3 actual está orientado principalmente a imágenes remotas que pasan por descarga y normalización.

Durante la implementación se debe elegir una de estas dos opciones sin duplicar mantenimiento:

1. extender `ImageCache` con una pequeña API para registrar/usar imágenes generadas localmente; o
2. añadir una ruta de archivo generado que viva bajo el mismo root y sea reconocida por la limpieza de inicio.

La opción elegida debe respetar el mantenimiento global ya existente.

No crear un segundo sistema LRU ni otro límite independiente salvo que aparezca una necesidad técnica real.

---

## 8. RAM / VRAM

El PNG puede permanecer en disco.

La textura Vita2D del QR **no** debe quedarse residente permanentemente.

Comportamiento:

```text
abrir modal
   ↓
cargar textura QR
   ↓
mostrar
   ↓
cerrar modal
   ↓
liberar textura de VRAM
   ↓
mantener PNG en disco
```

Estado conceptual:

```cpp
vita2d_texture* qrTexture_ = nullptr;
```

Al cerrar:

```cpp
vita2d_free_texture(qrTexture_);
qrTexture_ = nullptr;
```

Debe usarse el mismo sistema de liberación diferida si fuese necesario para respetar el ciclo GPU ya usado por `FullCatalogScreen`.

---

## 9. Integración con FullCatalogScreen

La ficha actual vive en:

```text
Client PSVitaAlive/source/ui/full_catalog_screen.cpp
```

La nueva función debe ser un modal sobre el detalle actual, no una nueva pantalla completa.

Estado propuesto en `FullCatalogScreen`:

```cpp
bool webQrVisible_ = false;
std::string webQrUrl_;
std::string webQrPath_;
vita2d_texture* webQrTexture_ = nullptr;
```

Funciones conceptuales:

```cpp
void openWebQrModal();
void closeWebQrModal();
void drawWebQrOverlay();
```

Si la lógica crece, separar la generación en `qr_code.cpp` y mantener en `FullCatalogScreen` solamente estado, input y render.

---

## 10. Sección Descarga externa (actualización aprobada)

La función se ofrece **siempre**, dentro de la lista seleccionable de la ficha
tanto de Homebrew como de PS Vita, PSP y PS1. Su posición fija es:

```text
INSTALL ALL / UPDATE / OPEN  (cuando corresponda)
DESCARGA EXTERNA
  [ Abrir en móvil / PC ]
DOWNLOADS
  enlaces originales...
```

- La sección permanece visible aunque no haya acción principal ni enlaces.
- Se navega con cruceta, X y pantalla táctil, siguiendo exactamente la
  semántica de las filas normales (primer toque selecciona, segundo activa).
- **No usar △** para abrir esta función; △ conserva su comportamiento previo.
- Su ubicación respeta el alto desplazable del panel Detail.

---

## 11. Modal

Diseño conceptual:

```text
┌──────────────────────────────────────────────────┐
│              Open on another device              │
│                                                  │
│                 █████████████                    │
│                 ██ QR CODE ██                    │
│                 █████████████                    │
│                                                  │
│ Scan this QR code with your phone                │
│                                                  │
│ https://vegettosan.github.io/PSVitaAlive/...     │
│                                                  │
│                    ○ Close                       │
└──────────────────────────────────────────────────┘
```

Debe incluir:

- fondo oscurecido;
- panel consistente con los demás overlays;
- título;
- QR;
- texto explicativo;
- URL visible;
- cierre con `○`;
- botón táctil de cierre.

Mientras esté abierto, el modal debe consumir input antes que la ficha inferior.

---

## 12. URL visible

La URL completa debe mostrarse debajo del QR.

Si no cabe en una línea:

- envolver visualmente el texto;
- no insertar caracteres dentro del valor real que se codifica;
- mantener `webQrUrl_` intacta.

El objetivo de mostrarla es permitir escribirla manualmente desde un PC cuando el usuario no pueda escanear el QR.

No se plantea copiar al portapapeles porque PS Vita no dispone de un flujo general adecuado para copiar la URL hacia otro dispositivo.

---

## 13. Localización

Todos los textos nuevos deben entrar en el sistema de localización actual.

Claves conceptuales:

```text
WEB_QR_BUTTON
WEB_QR_TITLE
WEB_QR_SCAN_HINT
WEB_QR_URL_HINT
WEB_QR_CLOSE
WEB_QR_GENERATING
WEB_QR_ERROR
```

Añadirlas a todos los idiomas empaquetados o proporcionar el fallback compatible con el sistema actual.

No dejar strings ingleses incrustados directamente en `full_catalog_screen.cpp`.

---

## 14. Errores

Casos mínimos:

### Falta identidad válida

Si no existe `titleId` para Homebrew o `id` para juegos:

```text
No se puede construir la página web.
```

No abrir un QR inválido.

### Falla la generación

Mostrar un error localizado y permitir cerrar el modal.

### Falla la carga del PNG

Liberar cualquier textura parcial y permitir regeneración en el siguiente intento.

### Escritura en disco no disponible

Se puede considerar generar la textura directamente en memoria como fallback futuro, pero la primera implementación debe priorizar simplicidad y comportamiento predecible.

---

## 15. Diagnóstico

Añadir logs discretos:

```text
[WebQR] open catalog=homebrew id=... title_id=...
[WebQR] cache hit path=...
[WebQR] generated path=...
[WebQR] generation failed reason=...
[WebQR] close
```

No registrar datos innecesarios.

La URL pública puede registrarse si resulta útil para diagnóstico.

---

## 16. Rendimiento

La implementación debe cumplir:

- cero generación QR al arrancar;
- cero generación QR al navegar por el catálogo;
- generar solo bajo solicitud explícita;
- un solo QR activo en VRAM;
- liberar la textura al cerrar;
- reutilizar PNG cacheado;
- no iniciar trabajo de red;
- no interferir con descargas VPK;
- no añadir threads salvo que las pruebas reales demuestren que el tiempo de generación produce un bloqueo visible.

En principio, generar un QR pequeño localmente debe poder hacerse de forma síncrona al abrir el modal.

Si en hardware real produce una pausa perceptible, entonces se podrá mover a un worker pequeño en una fase posterior.

---

## 17. Archivos previstos

Probable conjunto mínimo:

```text
Client PSVitaAlive/
├── include/ui/
│   ├── qr_code.hpp
│   └── web_page_link.hpp
├── source/ui/
│   ├── qr_code.cpp
│   ├── web_page_link.cpp
│   └── full_catalog_screen.cpp
├── third_party/qrcodegen/
│   ├── qrcodegen.c
│   ├── qrcodegen.h
│   └── LICENSE
├── assets/lang/
│   └── *.lang
└── CMakeLists.txt
```

`image_cache.hpp/.cpp` solo se modifica si finalmente hace falta una API explícita para imágenes generadas localmente.

---

## 18. Orden recomendado de implementación

No implementar todo de golpe.

### Fase 1 — URL

Crear y probar únicamente:

```text
CatalogType + CatalogItem → URL correcta
```

Validar Homebrew, Vita, PSP y PS1.

### Fase 2 — QR sin UI

Integrar la librería QR y comprobar que una URL de ejemplo produce un PNG escaneable.

Validar en PC/build y después en Vita/Vita3K.

### Fase 3 — Caché

Guardar el QR en el root de caché acordado.

Validar:

- primera generación;
- reutilización;
- regeneración después de eliminar el archivo;
- interacción con la limpieza de inicio.

### Fase 4 — Modal

Añadir overlay, input y touch.

Validar apertura/cierre sin afectar Detail.

### Fase 5 — Localización

Añadir todas las cadenas.

### Fase 6 — Hardware real

Probar:

- Vita 1000;
- Vita 2000 si es posible;
- distintos brillos;
- lectura del QR con varios teléfonos;
- tiempo de generación;
- memoria;
- cierre/reapertura repetidos;
- caché después de reiniciar.

No avanzar a la siguiente fase si la anterior rompe navegación, caché o estabilidad.

---

## 19. Criterios de aceptación

La función puede considerarse terminada cuando:

- Homebrew abre el QR de su ficha web correcta;
- Vita/PSP/PS1 generan la URL de su ficha correcta;
- el QR se escanea de forma fiable;
- la URL visible coincide exactamente con la codificada;
- la segunda apertura reutiliza el archivo cacheado;
- si el caché elimina el QR, se regenera sin error;
- cerrar el modal libera la textura;
- el catálogo continúa navegando con el mismo rendimiento;
- no se modifica ningún JSON oficial para soportar la función;
- no se agrega dependencia de servidor;
- todos los textos están localizados;
- no se rompe `Select links`, `Request Data`, búsqueda ni instalación.

---

## 20. Resumen de arquitectura

```text
CatalogItem + CatalogType
          ↓
   web_page_link
          ↓
      URL pública
          ↓
       qr_code
          ↓
 QR PNG local/cache
          ↓
   FullCatalogScreen
          ↓
 modal QR + URL + Close
```

La URL de la web es derivada, no almacenada en el catálogo.

El QR es una representación local de esa URL y no forma parte del contrato público de VitaHub/PSVitaAlive.

---

## 21. Estado al dejar esta documentación

A fecha de esta documentación:

- la idea está definida;
- las rutas web necesarias ya existen;
- el cliente ya tiene `CatalogType`, `CatalogItem`, modal/overlay patterns e Image Cache v3 que pueden reutilizarse;
- **la implementación QR está en una rama experimental, sin fusionar**;
- **el cliente se modifica únicamente en la rama de trabajo**;
- **el build de la rama incluye el generador QR en C**.

Cuando se retome, empezar por **Fase 1 — construcción y validación de URLs**, no por la UI.

---

## 22. Detalles de implementación de la rama

- Librería Nayuki (MIT) incluida como código fuente local en `third_party/qrcodegen`.
- `web_page_link.cpp` genera rutas según `CatalogType` (nunca JSON extra).
- `qr_code.cpp` guarda símbolos PNG de 1 píxel por módulo y 4 módulos blancos
  de margen en `cache/images/v3/<bucket>/qr_<hash>.png`.
- El control de caché v3 ya contabiliza PNG dentro de buckets de dos hexadecimales.
- La textura se carga solo al abrir, se amplía por múltiplos enteros y usa filtro
  POINT, se libera mediante el pipeline existente de liberación diferida.
- El modal grande incluye URL completa con ajuste visual por ancho, input exclusivo,
  botón táctil de cierre y localización en los ocho idiomas empaquetados.
- `tests/test_web_page_link.cpp` comprueba las cuatro clases de URLs.
- **Pendiente:** resultado de VitaSDK CI y pruebas en PS Vita/Vita3K. No fusionar a
  `main` sin ello.

---

## 23. Rediseño del modal QR: guía de descarga e instalación (octubre de 2026)

El usuario validó que la primera implementación genera el QR instantáneamente en hardware real.
Se aprobó una segunda iteración visual **sin cambiar** el botón Descarga externa:

- **Columna izquierda:** QR grande, negro sobre blanco y escalado por múltiplos
  enteros; debajo aparece la URL legible, envuelta por ancho. Si el enlace
  necesita más de cinco líneas, izquierda/derecha permite verlas todas.
- **Columna derecha:** título Guía de instalación y pasos locales derivados del
  tipo de enlace presente en el `CatalogItem`. Scroll exclusivo de instrucciones
  mediante cruceta arriba/abajo, analógico o arrastre táctil. Barra de
  desplazamiento solo cuando el contenido no cabe.
- **Pie:** cierre con círculo o botón táctil, sin superposición con ninguno
  de los paneles.
- El código de selección `makeWebQrGuide(CatalogType, CatalogItem)` vive en
  `include/ui/web_qr_guide.hpp` y `source/ui/web_qr_guide.cpp` para permitir
  pruebas nativas, independiente de GPU y VitaSDK.

### Reglas de contenido dinámico

1. Todos ven instrucciones para escanear QR, abrir ficha en teléfono/PC y
   descargar archivos desde allí.
2. Si se detecta un VPK, se indica transferencia USB/FTP e instalación con
   VitaShell.
3. Enlaces Game Files / Data Files añaden pasos sobre extracción ZIP y
   preservación de la estructura. **Solo** se imprime una ruta concreta si
   el propio enlace declara `extract_path`; por ejemplo `ux0:data/`.
   Se deduplican enlaces con mismo tipo y ruta. Sin ruta definida, pedir
   consultar la documentación del proyecto, sin inventar destino.
4. Para ISO/CSO de PSP y PBP compatible con Adrenaline, se ofrecen rutas
   convencionales de Adrenaline. Para PKG se evita recomendar instalarlos
   como VPK con VitaShell y se aclara que puede ser necesaria una licencia.
5. Si no hay tipos reconocidos, mostrar las instrucciones generales de la
   ficha del desarrollador.
6. Todas las frases están traducidas a EN, ES, FR, DE, IT, PT-PT, PT-BR y RU.
7. El texto se prepara una sola vez al abrir. La ruta QR, la identidad web,
   Image Cache v3 y los JSON oficiales no cambian.

### Pruebas

El test `tests/test_web_qr_guide.cpp` verifica variantes VPK, Game Files,
Data Files, destinos válidos/ausentes, enlaces duplicados, PSP ISO, PS1 PBP
y PKG. Su CI forma parte del workflow de compilación del cliente.

**Estado de aceptación:** pendiente comprobar alineación, anchuras reales de
fuentes, scroll táctil/cruceta, caminos largos y lectura del QR en la nueva
disposición en PS Vita física. No fusionar a `main` sin confirmar.
