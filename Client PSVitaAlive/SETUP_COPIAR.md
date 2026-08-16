# PS Vita Alive client setup and build guide

This file is the practical setup companion for `Client PSVitaAlive/README.md`. It is intentionally focused on building and validating the current client rather than describing the original bootstrap prototype.

## 1. Host environment

On Windows, WSL2 with Ubuntu is a practical development environment. On Linux/macOS, use the native shell and install the equivalent build tools.

Required tools include:

- Git
- CMake
- GNU Make
- Python 3 where required by the repository scripts
- VitaSDK

## 2. VitaSDK environment

Set the SDK path before building:

```bash
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"
```

Verify it:

```bash
echo "$VITASDK"
arm-vita-eabi-g++ --version
cmake --version
```

## 3. Build the client

From the repository root:

```bash
cd ~/PSVitaAlive/"Client PSVitaAlive"
rm -rf build
mkdir build
cd build
cmake ..
make -j1
```

The expected output is:

```text
PSVitaAlive.vpk
```

with Title ID `PSVA00001`.

`-j1` is recommended for diagnostics. Once the build is known to work, a parallel build may be used if desired.

## 4. LiveArea packaging

The VPK includes:

```text
sce_sys/
├── param.sfo
├── icon0.png
├── pic0.png
└── livearea/
    └── contents/
        ├── bg0.png
        ├── startup.png
        └── template.xml
```

The build uses an explicit `vita-pack-vpk` invocation with a relative source mapping so the repository can be built even when the local project directory contains spaces.

See `assets/sce_sys/README.md` for the exact LiveArea asset requirements.

## 5. Install and validate on Vita

1. Transfer the generated `PSVitaAlive.vpk` to the Vita with VitaShell/FTP or another trusted transfer method.
2. Install the VPK with VitaShell.
3. Launch PSVitaAlive from LiveArea.
4. Verify that the application icon, LiveArea background and startup image appear correctly.
5. Test the catalog and installation flows relevant to the current development phase.

## 6. Useful runtime paths

```text
ux0:data/psvitaalive/
├── logs/
│   ├── session.log
│   └── install.log
├── cache/
└── tmp/
```

## 7. Clean rebuild

When changing CMake, packaging assets or generated build metadata, use a clean build:

```bash
cd ~/PSVitaAlive/"Client PSVitaAlive"
rm -rf build
mkdir build
cd build
cmake ..
make -j1
```

Do not commit the generated `build/` directory unless the repository explicitly requires it.

## 8. Troubleshooting

### Generic `Error 2`

Run:

```bash
make VERBOSE=1 -j1
```

Look for the first real compiler, linker or packaging error above the final `Error 2` line.

### `vita-pack-vpk` segmentation fault

Check the generated command. The current CMake configuration intentionally uses a relative mapping:

```text
-a ../assets/sce_sys=sce_sys
```

Do not revert to absolute resource paths that contain the repository's `Client PSVitaAlive` space.

### `No rule to make target 'eboot.bin'`

The VitaSDK `vita_create_self(eboot.bin ...)` flow exposes the generated dependency as `eboot.bin-self` in the current toolchain. The VPK target must depend on `eboot.bin-self` before packaging `eboot.bin`.
