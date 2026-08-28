#!/usr/bin/env python3
"""One-shot: rewrite apply_news_md.py to use lambda replacements (literal)."""
from pathlib import Path

p = Path("scripts/apply_news_md.py")
s = p.read_text(encoding="utf-8")
old1 = '''    cpp2, n = re.subn(
        r"newsCheckedOnce_ = true;\\n    newsId_ = item\\.id;.*?visualNewsScroll_ = 0\\.f;",
        new_build,
        cpp,
        count=1,
        flags=re.S,
    )'''
new1 = '''    cpp2, n = re.subn(
        r"newsCheckedOnce_ = true;\\n    newsId_ = item\\.id;.*?visualNewsScroll_ = 0\\.f;",
        lambda _m: new_build,
        cpp,
        count=1,
        flags=re.S,
    )'''
# Simpler approach: replace the replacement argument only
if "lambda _m: new_build" not in s:
    s = s.replace(
        "        new_build,\n        cpp,\n        count=1,\n        flags=re.S,\n    )\n    if n != 1:",
        "        lambda _m: new_build,\n        cpp,\n        count=1,\n        flags=re.S,\n    )\n    if n != 1:",
        1,
    )
if "lambda _m: new_draw" not in s:
    s = s.replace(
        "        new_draw,\n        cpp,\n        count=1,\n        flags=re.S,\n    )\n    if n2 != 1:",
        "        lambda _m: new_draw,\n        cpp,\n        count=1,\n        flags=re.S,\n    )\n    if n2 != 1:",
        1,
    )
if "lambda _m: new_build" not in s or "lambda _m: new_draw" not in s:
    raise SystemExit("failed to insert lambda replacements")
p.write_text(s, encoding="utf-8")
print("apply_news_md.py fixed")
