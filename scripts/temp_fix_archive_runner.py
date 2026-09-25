from pathlib import Path

p = Path('scripts/temp_apply_archive_nodes_log.py')
text = p.read_text(encoding='utf-8')
old = "'''    if (urls.empty()) return false;\\n\\n    // The catalog/link size is intentionally only a threshold hint.\\n''',"
new = "'''    if (urls.empty()) return false;\\n\\n    // The catalog/link size is intentionally only a threshold hint. It never\\n''',"
if old not in text:
    raise SystemExit('runner pattern to fix was not found')
p.write_text(text.replace(old, new, 1), encoding='utf-8')
