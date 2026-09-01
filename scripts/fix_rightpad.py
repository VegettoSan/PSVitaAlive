from pathlib import Path

p = Path("Client PSVitaAlive/source/ui/full_catalog_screen.cpp")
text = p.read_text(encoding="utf-8")

old = (
    "        // Leave room on the right for Game Files / Data Files chips so the title never overlaps them.\n"
    "        int rightPad = 12;\n"
    "        if (itemHasLinkType(it, \"game files\") || itemHasLinkType(it, \"data files\")\n"
    "            || itemHasLinkType(it, \"game file\") || itemHasLinkType(it, \"data file\")) {\n"
    "            rightPad = 118; // ~\"Game Files\" chip width + margin\n"
    "        }"
)
new = (
    "        // Leave a modest right margin so long titles stay readable but do not spill\n"
    "        // under the Game Files / Data Files chips (chips sit mid/lower-right).\n"
    "        int rightPad = 12;\n"
    "        if (itemHasLinkType(it, \"game files\") || itemHasLinkType(it, \"data files\")\n"
    "            || itemHasLinkType(it, \"game file\") || itemHasLinkType(it, \"data file\")) {\n"
    "            rightPad = 64; // enough clearance; title keeps most of the row\n"
    "        }"
)

if old not in text:
    if "rightPad = 64" in text:
        print("already at 64")
    else:
        idx = text.find("rightPad")
        print("FAIL rightPad context:", repr(text[idx:idx+200] if idx >= 0 else "not found"))
        raise SystemExit(1)
else:
    text = text.replace(old, new)
    p.write_text(text, encoding="utf-8")
    print("OK rightPad 118 -> 64")
