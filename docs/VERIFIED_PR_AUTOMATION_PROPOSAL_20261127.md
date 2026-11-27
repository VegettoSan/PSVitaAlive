# PSVitaAlive — propuesta de automatización de PR Verified (2026-11-27)

**Estado: investigación aplazada por decisión del mantenedor. NO IMPLEMENTADO, NO ACTIVADO.** No se ha creado rama experimental, no se ha aprobado un workflow, no se ha aprobado/fusionado el PR #138 ni cambiado la política de GitHub. Este documento preserva ideas para una decisión posterior; no es autorización para ejecutar acciones.

## Motivación y caso de estudio

El [PR #138](https://github.com/VegettoSan/PSVitaAlive/pull/138), enviado por `noxgp`, propone una ficha y un autor nuevos (`apps/five-nights-at-freddy-s-1.json`, `authors/nox.json`). Es un colaborador nuevo (FIRST_TIME_CONTRIBUTOR). El workflow [#3773](https://github.com/VegettoSan/PSVitaAlive/actions/runs/46080935108) quedó en `action_required` sin jobs; no demuestra error de validación. La solicitud original queda intacta. Commit observado: 8ca5bf6512a757ae971406013e9d96ee7e538d17.

Datos propuestos y contrastados con [release oficial v1.0.6](https://github.com/noxgp/fnaf1_ps_vita/releases/tag/v1.0.6): versión 1.0.6, APP_VER declarado 00.28, Title ID declarado FNAFSCN01, VPK de 473727 bytes y SHA-256 publicado `cd2022ee22dd1d515ee2c738a29f64ef58b4396b66e610f7beff991303226586`. El repositorio y cuenta del PR coinciden. **No se ha descargado ni auditado internamente este VPK.** No hay icono ni capturas en el JSON propuesto; el validador actual admite su ausencia. El código del port se declara MIT; su README requiere copia Android 2.0.7 del usuario, datos preparados con Data Tool de PC, y que los datos comerciales no se distribuyan. Los recursos de presentación incluidos en el VPK publicado pueden requerir revisión de derechos por separado.

## Objetivo

Automatizar la revisión de propuestas Verified del desarrollador reutilizando la inspección de Orbit, sin convertir las aportaciones externas en código confiable ni crear otro catálogo. Separar identidad y validación de la aplicación, revisión de permisos de distribución, autorización de workflows, aprobación de la revisión y merge. No desactivar globalmente el requisito de aprobación de contribuciones externas.

## Diseño candidato, sujeto a revisión

1. Detectar PR abierto y fijar número, base y SHA exacto de head; leer el inventario completo de archivos modificados por API. Admitir solo nuevas fichas JSON en apps/ y, cuando corresponda, authors/; cualquier cambio de código, workflow, binario, configuración, eliminación, ruta inesperada o diff no inspeccionable va a revisión manual.
2. Ejecutar herramientas de confianza de PSVitaAlive, leer contenido del fork como datos acotados y validar esquema, nombres/rutas, autor y permisos. Identidad del remitente = propietario verificado por ID de cuenta/repo; colaboradores, forks y organizaciones ambiguas requieren autorización comprobable. No aceptar que el autor se autodeclare Verified sin evidencia.
3. Consultar fuente/release oficial, validar tamaño real y hash, Title ID, estructura, CRC y duplicados en main actual. Reutilizar `full_package()`, `native_media()`, `validate_vpk_title()`, validadores V2 y taxonomía existentes, sin alterar el nombre o metadatos válidos del autor por defecto. Los prereleases, VPK múltiples y actualizaciones de fichas existentes requieren política explícita separada.
4. Completar icono faltante desde icon0.png, o recurso oficial autorizado; conservar iconos válidos aportados. Buscar capturas reales en fuentes oficiales; no presentar un fondo de LiveArea como captura de gameplay. Validar tamaño/formato/dimensiones y procedencia de cada recurso. Si la autorización no está clara, mantener sin copiar o enviar a revisión manual. No incluir APK comercial, Game Files ni recursos no autorizados.
5. Generar un diff e informe de evidencia (PR head SHA, source repo ID, asset ID, tag, digest, validación, decisiones sobre imágenes). Validar una copia de todo el catálogo con el JSON enriquecido. No modificar el PR original ni main durante la prueba.
6. Si en el futuro se autoriza publicación real: adquirir el mismo bloqueo de publicadores, releer main y head, revalidar cambios/duplicados y permisos, evitar reintentos que dupliquen una alta. Determinar si el enriquecimiento será propuesta al autor o un commit controlado de mantenedor. No confundir un PR aprobado con el JSON enriquecido si este no está incluido en el commit final. Mantener auditoría de qué se publica, quién lo autorizó y a partir de qué SHA. El método de publicación/merge, permisos mínimos y forma de informar al autor aún no están decididos.

## Seguridad y alcance de una prueba

Un workflow privilegiado no debe ejecutar scripts del fork, hacer checkout de su head como código ejecutable ni exponer tokens/secretos. En particular no combinar `pull_request_target` con checkout/ejecución de código externo. Si se estudia ese evento, limitarlo a código del repositorio base confiable y lectura de datos externos. Aislar descarga/inspección de binarios, limitar dominios/redirecciones/tamaño y no ejecutar VPK. Tratar mensajes, README, enlaces y JSON del PR como entrada no confiable; evitar interpolarlos en shell. Separar etapa de lectura con permisos mínimos y etapa futura de escritura con autorización específica; validar la evidencia nuevamente en esta última. Documentación de referencia: [seguridad de GitHub Actions](https://docs.github.com/en/actions/security-for-github-actions/security-guides/security-hardening-for-github-actions), [eventos que disparan workflows](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows).

Rama propuesta (no creada): `feature/verified-pr-automation`. Primera prueba repetible a partir de los JSON fijados del PR #138, sin tocar ese PR. Configuración conceptual (no es un archivo operativo):

```yaml
mode: dry-run
pull_request: 138
auto_enrich: true
auto_approve: false
auto_merge: false
archive_upload: false
```

No se necesita gastar el único intento en main: los datos se pueden reproducir en una rama/copia de prueba, con el mismo SHA como fixture, sin depender de aprobar el workflow del autor. No es necesario ni deseable simular un merge real con privilegios en esta fase.

## Criterios antes de pedir autorización de implementación

- [ ] Verificar identidad del autor y FNAFSCN01 con el VPK oficial; SHA-256 y tamaño coincidentes.
- [ ] Generar informe y JSON enriquecido conservando requisitos, links, versión y autor; medios oficiales válidos, licencia/permiso explícito cuando corresponda.
- [ ] Detectar duplicado de Title ID, ID o autor retirado; conflicto si main cambia.
- [ ] Rechazar PR con scripts/workflows/archivos extraños, datos inválidos, rutas inseguras o repositorios discordantes.
- [ ] Rechazar evidencia si el head/release cambia; pruebas de errores/red y recuperación idempotente.
- [ ] Probar con fixtures positivos/negativos y con PR externo en modo de solo lectura; demostrar ausencia de escrituras sobre main, el PR original, Archive.org o Discord.
- [ ] Validar el catálogo completo V2 con el resultado; no editar catálogos generados.
- [ ] Definir explícitamente permisos, política de derechos/medios, caso de autor colaborador, mecanismo de aprobación y de publicación, límites de recursos, auditoría y apagado.
- [ ] Obtener nueva aprobación del mantenedor antes de activar cualquier automatización real.

Posibles estados de trabajo, no campos públicos: DETECTED, VALIDATING, ENRICHING, READY_TO_APPROVE, PUBLISHED, PENDING_MANUAL, BLOCKED. Posibles módulos separados de intake/validation/enrichment/publication y política/estado interno; son nombres propuestos, no archivos existentes.

Relacionado: [informe técnico de la automatización ya integrada](AUTOMATIC_CATALOG_PUBLICATION_20261127.md). La decisión vigente es dejar la solicitud quieta y reconsiderar el diseño antes de implementarlo.
