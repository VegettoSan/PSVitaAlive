#!/usr/bin/env python3
from pathlib import Path
ZE = Path(__file__).resolve().parents[1] / "Client PSVitaAlive/source/archive/zip_extractor.cpp"
t = ZE.read_text(encoding="utf-8")
if "psp2/io/stat.h" in t:
    print("already has stat.h")
else:
    if "#include <psp2/io/fcntl.h>" in t:
        t = t.replace(
            "#include <psp2/io/fcntl.h>",
            "#include <psp2/io/fcntl.h>\n#include <psp2/io/stat.h>",
            1,
        )
    else:
        t = t.replace(
            '#include "archive/zip_extractor.hpp"',
            '#include "archive/zip_extractor.hpp"\n#include <psp2/io/stat.h>',
            1,
        )
    ZE.write_text(t, encoding="utf-8")
    print("added psp2/io/stat.h")
