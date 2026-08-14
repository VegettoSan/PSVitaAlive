# PS Vita Alive Store

<p align="center">
  <img src="https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/web/assets/logo/PSVitaAlive_Store_logo_text.png" alt="PS Vita Alive Store" width="520">
</p>

<p align="center">
  <strong>Tienda alternativa, gratuita y abierta de Homebrew para PlayStation Vita.</strong>
</p>

<p align="center">
  Descubre, preserva, descarga y mantén actualizado tu Homebrew de PS Vita desde una arquitectura abierta basada únicamente en GitHub.
</p>

---

## ¿Qué es PS Vita Alive Store?

**PS Vita Alive Store** es una plataforma abierta para descubrir y distribuir Homebrew de PlayStation Vita. El proyecto está diseñado para ser gratuito, escalable y fácil de mantener sin depender de servidores propios.

El proyecto combina tres piezas principales:

- **Cliente nativo para PS Vita**, desarrollado con VitaSDK.
- **Sitio web estático**, publicado mediante GitHub Pages.
- **Pipeline de catálogo**, automatizado con GitHub Actions.

La idea central es que desarrolladores, colaboradores y preservadores puedan mantener aplicaciones individuales sin tener que editar manualmente un catálogo gigante.

---

## ✨ ¿Qué ofrece?

### Para usuarios

- 🔎 Buscar Homebrew por nombre, Title ID, autor o categoría.
- 🗂️ Explorar categorías y subcategorías propias de PS Vita Alive Store.
- 👤 Consultar perfiles individuales de desarrolladores.
- 🖼️ Ver iconos y screenshots.
- 📦 Encontrar múltiples fuentes de descarga.
- ⭐ Identificar la descarga recomendada.
- 📅 Consultar versión y fecha de publicación.
- 🔄 Recibir información actualizada desde múltiples catálogos externos.
- 🕰️ Conservar proyectos antiguos mediante estados `Legacy` y `Archive`.
- ✅ Identificar proyectos mantenidos mediante el flujo `Verified`.

### Para desarrolladores y colaboradores

- Un archivo JSON individual por aplicación.
- Un perfil JSON individual por autor.
- Categorías y subcategorías controladas por el proyecto.
- Pull Requests para registrar proyectos mantenidos.
- Overrides para corregir o enriquecer información sin perder las actualizaciones externas.
- Posibilidad de mantener varios enlaces de descarga, repositorio, documentación, mirrors, datos adicionales, etc.
- Integración automática de fuentes externas sin tener que copiar manualmente sus catálogos.

---

## 🏗️ Arquitectura

La arquitectura canónica es deliberadamente sencilla:

```text
apps/
authors/
categories/
    │
    ├───────────────┐
    │               │
    │        Fuentes externas
    │        VitaDB / VitaDBtoo /
    │        NeoVitaDB / otras
    │               │
    └───────┬───────┘
            ▼
     Normalización
            ▼
      Deduplicación
       por Title ID
            ▼
   Comparación de versiones
            ▼
    Merge / Enrichment
            ▼
        Overrides
            ▼
       Validación
            ▼
  ┌──────────────────────────────┐
  │ catalog.json                 │
  │ authors.json                 │
  │ categories.json              │
  └──────────────────────────────┘
            │
       ┌────┴────┐
       ▼         ▼
     Web      Cliente PS Vita
```

Los tres catálogos finales son **archivos generados**. No deben editarse manualmente.

La fuente de verdad está en los archivos individuales de `apps/`, `authors/` y `categories/`, junto con las reglas de normalización y los Overrides.

---

## 🔄 Catálogos externos

PS Vita Alive Store puede importar información de diferentes fuentes de Homebrew.

Actualmente se contemplan:

- **VitaDB**
- **NeoVitaDB**
- **VitaDBtoo**

Los formatos externos no se consumen directamente por la web ni por el cliente. Cada fuente se transforma al modelo interno antes de incorporarse al catálogo.

Esto permite que una fuente externa esté incompleta sin destruir información propia de PS Vita Alive Store.

### Actualización y deduplicación

Si varias fuentes contienen la misma aplicación, se utiliza `title_id` para identificarla y evitar duplicados.

Cuando existen diferentes versiones, la lógica de agregación compara principalmente:

1. versión;
2. fecha de versión;
3. prioridad de fuente como desempate.

Por ejemplo, una fuente con menor prioridad puede proporcionar la descarga recomendada si contiene realmente una versión más reciente.

---

## 🧩 Overrides

Los Overrides permiten mantener información propia aunque las fuentes externas no la proporcionen.

Ejemplo de uso:

```text
catalog_overrides/
└── mi-aplicacion.json
```

Un Override puede añadir o corregir, entre otros:

- descripción;
- descripción larga;
- screenshots;
- icono;
- requisitos;
- changelog;
- enlaces adicionales;
- datos de juegos necesarios para un port;
- fuentes alternativas de descarga.

El flujo es:

```text
Fuente externa
      ↓
Merge / enriquecimiento
      ↓
Override
      ↓
Aplicación canónica
```

Así una aplicación puede seguir recibiendo automáticamente nuevas versiones mientras conserva información manual que no existe en los catálogos externos.

---

## 👥 Autores individuales

Los autores se mantienen como entidades independientes.

Una aplicación con varios desarrolladores utiliza varios IDs:

```json
"author_ids": [
  "autor-a",
  "autor-b"
]
```

No se crea un perfil combinado como `autor-a & autor-b`.

Esto permite que la web y el cliente puedan abrir el perfil de cada desarrollador y mostrar todas sus aplicaciones.

Cuando una fuente externa descubre un autor que todavía no existe, el pipeline puede crear un perfil mínimo y posteriormente enriquecerlo con la información disponible.

---

## 🖼️ Recursos y fallbacks

Las imágenes externas se normalizan a URLs públicas válidas cuando la fuente realmente publica esos recursos.

No se asume que una ruta relativa como:

```text
screenshots/screenshot1.png
```

existe dentro de GitHub Pages.

También existen fallbacks para evitar interfaces rotas cuando una imagen externa deja de estar disponible:

- avatar de autor → `authors/icon/autoricon.png`;
- icono de aplicación → icono de su categoría;
- fallback visual adicional en la interfaz web.

---

## 🌐 Sitio web

El sitio web está construido con:

- HTML
- CSS
- JavaScript
- GitHub Pages

No necesita un backend propio.

Consume únicamente:

```text
catalog.json
authors.json
categories.json
```

El código web se encuentra en [`web/`](web/README.md).

---

## 🎮 Cliente PS Vita

El cliente nativo se desarrolla con:

- VitaSDK
- C
- C++
- CMake
- vita2d
- libcurl
- otras librerías de VitaSDK según la fase del proyecto

El cliente está diseñado para consumir los catálogos generados, buscar aplicaciones, mostrar información y posteriormente gestionar descargas, instalaciones y actualizaciones.

La documentación del cliente está en [`Client PSVitaAlive/`](Client%20PSVitaAlive/README.md).

---

## ⚙️ Automatización

GitHub Actions se encarga de:

- leer fuentes externas;
- normalizar registros;
- deduplicar aplicaciones;
- comparar versiones;
- resolver autores;
- aplicar Overrides;
- persistir cambios canónicos;
- validar JSON, IDs, recursos y enlaces;
- generar los catálogos finales;
- publicar GitHub Pages cuando corresponde.

El sistema también utiliza concurrencia para evitar que dos ejecuciones intenten publicar cambios simultáneamente.

---

## 📚 Documentación

La documentación está distribuida junto a cada área para que sea posible entender una parte del proyecto sin tener que estudiar todo el repositorio.

- [`apps/`](apps/README.md) — estructura y reglas de las aplicaciones.
- [`authors/`](authors/README.md) — perfiles, múltiples autores y fallbacks.
- [`categories/`](categories/README.md) — categorías y subcategorías oficiales.
- [`catalog_overrides/`](catalog_overrides/README.md) — enriquecimiento y correcciones manuales.
- [`sources/`](sources/README.md) — fuentes externas y reglas de importación.
- [`external_authors/`](external_authors/README.md) — identidad provisional de autores externos.
- [`scripts/`](scripts/README.md) — generación, normalización y validación.
- [`scripts/external/`](scripts/external/README.md) — motor de integración de catálogos externos.
- [`web/`](web/README.md) — sitio web y GitHub Pages.
- [`Client PSVitaAlive/`](Client%20PSVitaAlive/README.md) — cliente PS Vita.
- [`.github/workflows/`](.github/workflows/README.md) — automatización de Actions.
- [`docs/`](docs/README.md) — decisiones técnicas y memoria del proyecto.

---

## 🤝 Contribuir

Antes de modificar el proyecto:

1. Lee el `README.md` de la carpeta que vas a modificar.
2. No edites manualmente los catálogos generados.
3. Si el cambio afecta a una aplicación, trabaja sobre `apps/` o `catalog_overrides/` según corresponda.
4. Si el cambio afecta a un autor, utiliza su JSON individual.
5. Si el cambio afecta a una fuente externa, modifica su adaptador/normalizador y documenta el comportamiento.
6. Ejecuta las validaciones antes de publicar.
7. Comprueba que web y cliente continúan consumiendo el mismo contrato de catálogo.

---

## 📌 Filosofía del proyecto

PS Vita Alive Store busca mantener una infraestructura:

- gratuita;
- abierta;
- sin servidores propios;
- basada en servicios gratuitos de GitHub;
- modular;
- escalable;
- orientada a preservación;
- compatible con Homebrew nuevo y antiguo.

El objetivo no es solamente crear otra lista de descargas: es construir una base de datos mantenible y una herramienta de preservación para el ecosistema Homebrew de PlayStation Vita.

---

## Estado del proyecto

El repositorio evoluciona por fases. La arquitectura de catálogo y el pipeline de integración externa ya están establecidos, mientras el cliente PS Vita continúa desarrollándose por etapas.

Consulta la documentación específica antes de implementar una nueva fase.

---

<p align="center">
  <sub>PS Vita Alive Store — Free & Open PlayStation Vita Homebrew Store</sub>
</p>
