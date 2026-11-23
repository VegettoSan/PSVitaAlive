# Archive.org — Primera prueba real (un solo VPK)

**Estado:** código experimental en rama `feature/archive-org-one-vpk-pilot`. Antes de fusionar, únicamente pruebas offline. Al fusionar este workflow a `main` **con la cuenta VegettoSan**, se intentará una única subida al ítem autorizado si todas las comprobaciones son correctas.

## Objeto exacto

- Aplicación: `better-homebrew-browser`, Title ID `BHBB00001`.
- Versión: `1.46`. VPK oficial: `BetterHomebrewBrowser.vpk`.
- Fuente: https://github.com/Ibrahim778/BetterHomebrewBrowser/releases/download/V1.46/BetterHomebrewBrowser.vpk
- Repositorio original: https://github.com/Ibrahim778/BetterHomebrewBrowser — indica licencia **GPL-3.0**.
- Tamaño esperado exacto: **1.129.327 bytes** (aprox. **1,077 MiB**).
- Destino existente administrado: https://archive.org/details/psvitaalive-better-homebrew-browser
- **Nombre de archivo nuevo**: `BetterHomebrewBrowser v1.46.vpk` (sin borrar ni sustituir `BetterHomebrewBrowser.vpk`).
- Este primer ensayo **no edita `apps/` ni cambia el enlace de descarga**. El objetivo es verificar transporte, credenciales IA-S3 y disponibilidad real del archivo nuevo.

## Bloqueos antes de escribir

1. Una subida histórica propia positiva debe vincular la app al identificador exacto del ítem.
2. La versión, Title ID, tamaño y URL de GitHub deben coincidir exactamente con el JSON canónico; nada de URLs arbitrarias, iconos ni Game Files.
3. La metadata pública del ítem debe mostrar el `uploader` de `IA_EXPECTED_UPLOADER` (secreto GitHub). No aparecerá en logs. Comprobar dos veces, antes y después de descargar.
4. Si el nombre remoto ya existe, abortar sin sobrescribir; el resultado será `ALREADY_EXISTS_REVIEW`.
5. Descargar únicamente HTTPS de GitHub y los hosts oficiales de assets, máximo 5 MiB; exigir tamaño conocido y cabecera ZIP/VPK.
6. Realizar una sola tentativa de subida con IA-S3, sin reintentar automáticamente en un resultado incierto.
7. Confirmar aparición del archivo con el tamaño y MD5 exactos; si Archive.org demora, registrar `VERIFYING` y no reenviar a ciegas.
8. GitHub Actions solo ejecutará este workflow de escritura en `main` por el actor `VegettoSan`, con token GitHub `contents: read` y sin PRs. Concurrencia individual; secretos disponibles únicamente durante la transferencia.

**Dependencia:** usar `internetarchive==5.13.0` desde PyPI. Este paquete permite `Item.upload_file(..., key=..., verify=True)`.

## Qué significa cada resultado

- `UPLOADED_VERIFIED`: la subida del único VPK fue aceptada y la metadata de IA coincide (sin cambio de catálogo).
- `ALREADY_EXISTS_REVIEW`: ese nombre ya existe en el ítem; no se ha sobrescrito.
- `PENDING_MANUAL`: origen, permisos, destino o tamaño conflictivos; requiere revisión.
- `WAITING_ARCHIVE`: Archive.org temporalmente inaccesible.
- `WAITING_SOURCE`: fallo al descargar el VPK original.
- `UPLOAD_RESULT_UNCERTAIN`: hubo intento de subida pero la respuesta fue incierta; inspeccionar solo este archivo antes de reintentar.
- `VERIFYING`: subida intentada, archivo todavía sin metadata íntegra.

El sistema por lotes sigue desactivado hasta validar este experimento. Nunca reinterpretar el resultado como autorización genérica para subir a ítems ajenos. Una vez validado, implementar reconciliación de URL de la versión actual con el JSON de `apps/`, luego lotes y pendientes.
