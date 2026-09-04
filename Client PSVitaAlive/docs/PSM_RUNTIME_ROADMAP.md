# PSM Runtime — Roadmap y documentación

## Estado actual

**Estado:** Pausado / reservado para una futura fase de soporte de juegos PlayStation Mobile (PSM).

PSVitaAlive no necesita instalar actualmente el PSM Runtime como requisito general para Homebrew/vitaGL. El objetivo original de esta integración era facilitar la obtención de `libshacccg.suprx` mediante ShaRKF00D. Ese objetivo ya no requiere instalar el Runtime porque PSVitaAlive dispone de un flujo de plugins esenciales que puede instalar directamente `libshacccg.suprx` en su ubicación correspondiente.

Esto **no significa que PSM Runtime sea inútil**. El Runtime mantiene una finalidad independiente: proporcionar el entorno necesario para ejecutar software PlayStation Mobile. Por ese motivo se conserva esta documentación y la arquitectura prevista para retomarlo cuando PSVitaAlive incorpore un catálogo de juegos PSM.

## Lo que NO debe hacerse ahora

- No instalar automáticamente PSM Runtime para Homebrew que solo requieren `libshacccg.suprx`.
- No eliminar todavía de forma agresiva los componentes experimentales relacionados con PSM Runtime hasta comprobar sus referencias en CMake/build y retirar únicamente el código que quede sin uso.
- No modificar el flujo normal de VPK, ZIP o PKG.
- No convertir el sistema de plugins esenciales en una dependencia del PSM Runtime.

## Relación con libshacccg

Flujo histórico:

```text
PSM Runtime
    ↓
ShaRKF00D
    ↓
libshacccg.suprx
    ↓
vitaGL / Homebrew que lo requieren
```

Flujo actual recomendado:

```text
PSVitaAlive
    ↓
Plugin esencial: libshacccg.suprx
    ↓
ur0:data/
    ↓
vitaGL / Homebrew que lo requieren
```

Por tanto, `libshacccg.suprx` debe tratarse como un componente de compatibilidad independiente del PSM Runtime.

## Futuro: soporte de juegos PSM

Cuando PSVitaAlive incorpore un catálogo de juegos PlayStation Mobile, se podrá retomar esta funcionalidad como un requisito específico del ecosistema PSM.

La arquitectura prevista será:

```text
Catálogo PSM
    ↓
Detección de requisitos
    ↓
¿PSM Runtime instalado?
    ├── Sí → continuar con la instalación del juego
    └── No → ofrecer instalación del Runtime
                 ↓
          Runtime 1.00
                 ↓
          Runtime 2.00
                 ↓
          Runtime 2.01
                 ↓
          validar instalación
                 ↓
          continuar con juego PSM
```

## Fases futuras

### Fase 1 — Revisión del soporte PSM

- Identificar los juegos PSM que se incorporarán al catálogo.
- Determinar qué versiones del Runtime requieren.
- Confirmar si el conjunto estándar 1.00 → 2.00 → 2.01 cubre los casos que queremos soportar.
- Documentar Title IDs, paquetes, tamaños, hashes y fuentes primarias.

### Fase 2 — Fuentes de los paquetes

- Definir las fuentes oficiales/archivísticas de los tres PKG.
- Mantener las URLs fuera del código de instalación siempre que sea posible.
- Utilizar la configuración dedicada de `psm_runtime_packages.json`.
- Registrar hashes y tamaños para validar descargas.
- No modificar fuentes externas originales para adaptarlas a VitaHub.

### Fase 3 — Instalador PSM aislado

- Recuperar el `PsmRuntimeInstaller` existente si sigue siendo válido.
- Mantenerlo separado de `VitaInstaller` para no alterar el flujo general de PKG.
- Preparar los paquetes en almacenamiento temporal.
- Ejecutar la instalación mediante el Package Installer del sistema cuando sea necesario.
- Mantener el orden 1.00 → 2.00 → 2.01.

### Fase 4 — Integración con Settings

- Añadir una sección específica de PSM Runtime en Settings.
- Mostrar estado instalado/no instalado.
- Validar los tres paquetes antes de instalar.
- Permitir descargar e instalar desde Settings.
- Mostrar progreso y errores específicos.
- No mezclar este flujo con la instalación de plugins esenciales.

### Fase 5 — Integración con juegos PSM

- Cuando el usuario intente instalar un juego PSM, comprobar el Runtime.
- Si falta, ofrecer instalarlo antes de continuar.
- Evitar reinstalaciones innecesarias.
- Validar que la versión requerida sea compatible.
- Mantener la instalación del juego separada de la instalación del Runtime.

### Fase 6 — Validación

- Probar en Vita3K.
- Probar en PS Vita física.
- Verificar Runtime 1.00, 2.00 y 2.01.
- Verificar ejecución de al menos un título PSM representativo.
- Verificar que la instalación del Runtime no afecte VPK, ZIP, PKG ni plugins.
- Verificar recuperación ante descarga incompleta o paquete corrupto.

## Archivos relacionados

- `config/psm_runtime_packages.json` — configuración de los tres PKG y su orden.
- `config/psm_runtime_settings.json` — configuración de la funcionalidad reservada.
- `PsmRuntimeInstaller` — implementación experimental/aislada del instalador.
- `psm_runtime_driver.skprx` — componente experimental relacionado con el flujo del Package Installer.

Estos componentes deben permanecer aislados hasta que se retome el soporte PSM. Los catálogos generados oficialmente siguen siendo responsabilidad de GitHub Actions y nunca deben editarse manualmente.

## Regla de arquitectura

El soporte futuro de PSM Runtime debe integrarse como una capacidad específica de PSM, no como requisito global de PSVitaAlive ni de vitaGL.

```text
apps/ + authors/ + categories/
        ↓
GitHub Actions
        ↓
catalog.json + authors.json + categories.json
        ↓
Cliente PS Vita + Web
        ↓
Juego PSM
        ↓
PSM Runtime (solo si es necesario)
```
