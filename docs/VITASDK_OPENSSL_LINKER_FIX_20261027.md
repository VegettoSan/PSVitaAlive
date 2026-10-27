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

## 2.b Segunda dependencia de enlace (descubierta tras corregir OpenSSL)

El nuevo build del PR [#60](https://github.com/VegettoSan/PSVitaAlive/pull/60) con OpenSSL 1.1.1 pasó los símbolos OpenSSL que antes fallaban, pero se detuvo en la **misma fase final de enlace** por nuevos símbolos sin resolver:

- `libstdc++.a(guard.o)` → `pthread_once`;
- `libcrypto.a(threads_pthread.o)` → `pthread_rwlock_init`, `pthread_rwlock_rdlock`, `pthread_rwlock_wrlock`, `pthread_rwlock_unlock`, `pthread_rwlock_destroy`, `pthread_once` y `pthread_atfork`.

Evidencia: [workflow run 40531759991, build de PR #60](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40531759991). No hay referencias sin resolver a OpenSSL en este segundo intento. El cliente compiló sus fuentes y llegó al enlazador (95 %).

**Causa adicional:** los archivos `libcrypto.a` y `libstdc++.a` de la imagen usan pthread; el `pthread` estático se especificaba antes de ambos en `target_link_libraries()` y el enlazador GNU suele inspeccionar los archivos `.a` de izquierda a derecha una sola vez. El soporte OpenSSL 1.0.2 usado antiguamente no exponía esta combinación de referencias.

**Cambio mínimo de CMake en PR #60:** se conserva **una única** referencia a la biblioteca `pthread`, movida detrás de OpenSSL y junto con `stdc++` al grupo:

```cmake
target_link_libraries(${PROJECT_NAME}
  "-Wl,--start-group"
  stdc++
  pthread
  "-Wl,--end-group"
)
```

El grupo se coloca **después** de `ssl crypto` / `mbedtls mbedx509 mbedcrypto`. Así el enlazador puede resolver el pequeño ciclo de dependencias de estos archivos estáticos. No cambia el comportamiento del código C++, el modelo de hilos ni el TLS; tampoco añade `-pthread` global o una segunda biblioteca `pthread` que pueda ocasionar definiciones duplicadas.

**Validación completada:** [Build PS Vita Client #508 — success](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40532684582). El job compiló, enlazó, generó `PSVitaAlive.vpk` y subió el artefacto. El [PR #60](https://github.com/VegettoSan/PSVitaAlive/pull/60) fue fusionado en `main` mediante [commit `151a224`](https://github.com/VegettoSan/PSVitaAlive/commit/151a22465265ea00707566ea05d2d4a2f64ab541).

---

## 3. Qué NO cambia

- No se modifican las peticiones de red, el tratamiento HTTPS, Range o las verificaciones de tamaño/checksum.
- No cambia el instalador, los VPK, la detección de actualizaciones ni los módulos de plugins.
- No se editan `apps/`, `authors/`, `categories/`, ni los JSON `catalog.json`, `authors.json`, `categories.json` generados por Actions.
- No se desactivan errores de compilación, no se ignora el estado de enlace, no se eliminan bibliotecas del proyecto.

## 4. Verificación final y límites

| Control | Estado |
| --- | --- |
| Raíz del fallo anterior identificada por log | Confirmada |
| Corrección del workflow OpenSSL en `main` | Aplicada: [`d30c9a1`](https://github.com/VegettoSan/PSVitaAlive/commit/d30c9a1d7bdb5357db57a61aaa68ab9e0510afae) |
| Unit tests que preceden al paso VitaSDK | Correctos en la ejecución final |
| Compilación y enlazado completos del cliente con ambos ajustes | **Éxito**: [CI 40532684582](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40532684582) |
| VPK producido y adjuntado por GitHub Actions | **Éxito**: artefacto `PSVitaAlive-image-throughput-16abd03c10d57cd7094652d72ac80a946b51a110` (ZIP de ~10,1 MB) |
| Comportamiento TLS de la nueva compilación en una PS Vita física | No probado aquí |

El [artefacto de la compilación](https://github.com/VegettoSan/PSVitaAlive/actions/runs/40532684582#artifacts) fue publicado correctamente. El éxito de CI confirma que genera un VPK y supera los tests existentes, **no** confirma por sí mismo su funcionamiento en una PS Vita real. Las comprobaciones de conexiones TLS/MediaFire/Archive.org en hardware siguen pendientes.

## 5. Reproducción y mantenimiento

El repositorio realiza una compilación PS Vita desde GitHub Actions mediante el contenedor oficial. Para reproducir **el mismo entorno**, conservar el tag seleccionado y evitar mezclar paquetes OpenSSL de distintas generaciones en el mismo `$VITASDK`.

Si cambia la versión de OpenSSL de un nuevo tag, primero comprobar el ABI de la `libcurl.a` provista (funciones solicitadas con `nm`) y sus bibliotecas SSL empaquetadas; **nunca** ejecutar `pacman -Rdd` como un arreglo genérico.

Los builds antiguos o máquinas locales aún pueden usar OpenSSL 1.0.2 si su `libcurl` fue enlazada para esa versión: los paquetes deben ser compatibles **entre sí**, no obligatoriamente coincidir con la versión histórica de los README.

**Documentación relacionada:** [Notas TLS](NETWORK_TLS.md) · [Avisos de plugins del catálogo](CATALOG_PLUGIN_CONFLICTS_20261027.md) · [Cliente](../Client%20PSVitaAlive/README.md).
