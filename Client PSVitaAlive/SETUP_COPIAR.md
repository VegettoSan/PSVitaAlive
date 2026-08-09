# Cómo copiar este proyecto a tu WSL

Desde PowerShell o desde el explorador de Windows, la carpeta generada está en el entorno de trabajo del asistente.  
Copia el contenido de `PSVitaAlive/` a:

```text
\\wsl.localhost\Ubuntu\home\vegettosandev\PSVitaAlive\
```

O dentro de WSL:

```bash
# Si descargas/copias el árbol a /mnt/c/... ajústalo.
# Ejemplo si tienes el zip en Downloads:
cd ~
# Asegúrate de que ~/PSVitaAlive existe
mkdir -p ~/PSVitaAlive
# Copia archivos (ajusta origen)
# cp -r /ruta/al/PSVitaAlive/* ~/PSVitaAlive/
```

## Comandos exactos en tu Ubuntu WSL

```bash
cd ~/PSVitaAlive

# Si ya tenías un main vacío, este proyecto lo sustituye de forma controlada.
# Revisa que queden estos archivos:
#   CMakeLists.txt
#   source/main.cpp
#   source/storage/storage_manager.cpp
#   include/storage/storage_manager.hpp
#   README.md

mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

Si `cmake` o `make` fallan, pega el error completo.

## Validación en Vita

1. Instala el nuevo `PSVitaAlive.vpk`
2. Ábrelo (pantalla en negro / mínima; espera y pulsa ✕)
3. Con VitaShell abre:
   - `ux0:data/psvitaalive/test/summary.txt`
4. Debe decir algo como:
   ```text
   dirs=1 write=1 read=1 size=1
   ```

Cuando eso esté OK, pasamos a **Fase 2: HttpClient**.
