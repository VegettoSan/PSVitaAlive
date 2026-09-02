#!/usr/bin/env python3
from pathlib import Path
CPP = Path("Client PSVitaAlive/source/installer/install_controller.cpp")
cpp = CPP.read_text(encoding="utf-8")
old = "sceKernelDelayThread(1500 * 1000); // 1.5s settle"
new = "sceKernelDelayThread(3000 * 1000); // 3s settle — margin for slower SD2Vita/USB"
if old not in cpp:
    if "3000 * 1000" in cpp and "settle" in cpp:
        print("already 3s")
        raise SystemExit(0)
    raise SystemExit("settle delay line not found")
cpp = cpp.replace(old, new, 1)
CPP.write_text(cpp, encoding="utf-8")
print("OK settle delay -> 3s")
