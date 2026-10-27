# PSVitaAlive — Fallo de enlazado libcurl/OpenSSL en VitaSDK CI (27-10-2026)

**Causa confirmada en el log del workflow:** mezcla de bibliotecas OpenSSL incompatibles con la versión de libcurl provista por la imagen oficial.

**Corrección ya aplicada a `main`:** commit [`d30c9a1`](https://github.com/VegettoSan/PSVitaAlive/commit/d30c9a1d7bdb5357db57a61aaa68ab9e0510afae), en `.github/workflows/client-build.yml`. Se elimina la sustitución manual de la biblioteca y se conserva la pila preinstalada en `vitasdk/vitasdk:2026.08`.

## 1. Evidencia

Run fallido antes de la corrección: [Build PS Vita Client #499, ejecución 40523917855](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40523917855).

Secuencia real del log:

1. El contenedor oficial `vitasdk/vitasdk:2026.08` inicia con `openssl-1.1.1`.
2. El workflow ejecutaba `vdpm pacman -- -Rdd --noconfirm openssl-1.1.1`: **eliminaba el paquete con `-Rdd` sin validar las dependencias**.
3. Después instalaba `openssl` `1.0.2.r12304...` desde el repositorio de paquetes. La salida confirmaba ambas operaciones.
4. Todos los fuentes de PSVitaAlive llegaron a compilar. **El enlazador** falló al unir `libcurl.a(openssl.c.obj)` con la versión antigua de `libssl.a`/`libcrypto.a`.

Ejemplos literales de símbolos ausentes: `OPENSSL_init_ssl`, `BIO_get_data`, `BIO_meth_new`, `SSL_CTX_set_default_read_buffer_len`, `HMAC_CTX_new`, `X509_STORE_up_ref`.

Estos nombres no prueban un fallo en `full_catalog_screen.cpp` ni en `plugin_detector.cpp`. El problema es de **ABI/versionado en el toolchain de CI**, provocado por el script de instalación de paquetes, no por los avisos de conflicto de plugins.

## 2. Modificación mínima

En `.github/workflows/client-build.yml`, paso `Build with official VitaSDK image`:

**Se retiró:**

```bash
"$VDPM" pacman -- -Rdd --noconfirm openssl-1.1.1 || true
VDPM_NONINTERACTIVE=1 "$VDPM" install openssl
```

**Ahora se conserva el paquete nativo** del contenedor y se comprueba temprano que la biblioteca esperada está presente:

```bash
"$VDPM" pacman -- -Q openssl-1.1.1
test -s "$VITASDK/arm-vita-eabi/lib/libssl.a"
test -s "$VITASDK/arm-vita-eabi/lib/libcrypto.a"
```

Se mantiene **exactamente el mismo tag oficial de VitaSDK 2026.08** y las opciones de configuración `cmake -S . -B build-ci -DCMAKE_BUILD_TYPE=Release`.

La opción predeterminada de CMake sigue enlazando las bibliotecas `ssl` y `crypto`; **no se cambia a mbedTLS**, no se modifica el backend de `curl` en tiempo de ejecución ni se alteran las opciones de red existentes.

La comprobación `vdpm ... -Q` hará fallar claramente el job si una futura imagen flotante deja de incluir la versión compatible conocida, en lugar de cambiar el paquete por la fuerza.

## 3. Qué NO cambia

- No se modifican las peticiones de red, el tratamiento HTTPS, Range o las verificaciones de tamaño/checksum.
- No cambia el instalador, los VPK, la detección de actualizaciones ni los módulos de plugins.
- No se editan `apps/`, `authors/`, `categories/`, ni los JSON `catalog.json`, `authors.json`, `categories.json` generados por Actions.
- No se desactivan errores de compilación, no se ignora el estado de enlace, no se eliminan bibliotecas del proyecto.

## 4. Confirmaciones pendientes o históricas

| Control | Estado |
| --- | --- |
| Raíz del fallo anterior identificada por log | Confirmada |
| Corrección limitada al workflow de CI en `main` | Aplicada |
| Unit tests que preceden al paso VitaSDK | Se ejecutaron durante el build fallido |
| Compilación y enlazado completos del cliente con la corrección | **Requiere nuevo CI verde** |
| VPK producido y adjuntado por GitHub Actions | **Requiere nuevo CI verde** |
| Comportamiento TLS de la nueva compilación en una PS Vita física | No probado aquí |

Una vez haya un nuevo build verde, anexar enlace de esa ejecución y distinguirlo expresamente de una prueba en hardware.

## 5. Reproducción y mantenimiento

El repositorio realiza una compilación PS Vita desde GitHub Actions mediante el contenedor oficial. Para reproducir **el mismo entorno**, conservar el tag seleccionado y evitar mezclar paquetes OpenSSL de distintas generaciones en el mismo `$VITASDK`.

Si cambia la versión de OpenSSL de un nuevo tag, primero comprobar el ABI de la `libcurl.a` provista (funciones solicitadas con `nm`) y sus bibliotecas SSL empaquetadas; **nunca** ejecutar `pacman -Rdd` como un arreglo genérico.

Los builds antiguos o máquinas locales aún pueden usar OpenSSL 1.0.2 si su `libcurl` fue enlazada para esa versión: los paquetes deben ser compatibles **entre sí**, no obligatoriamente coincidir con la versión histórica de los README.

**Documentación relacionada:** [Notas TLS](NETWORK_TLS.md) · [Avisos de plugins del catálogo](CATALOG_PLUGIN_CONFLICTS_20261027.md) · [Cliente](../Client%20PSVitaAlive/README.md).
