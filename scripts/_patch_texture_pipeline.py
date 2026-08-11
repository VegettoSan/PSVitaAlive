from pathlib import Path
import subprocess

path = Path('Client PSVitaAlive/source/ui/full_catalog_screen.cpp')
text = path.read_text(encoding='utf-8')
old = 'constexpr size_t MAX_APP_TEXTURES=6,MAX_SCREENSHOT_TEXTURES=3;'
new = 'constexpr size_t MAX_APP_TEXTURES=9,MAX_SCREENSHOT_TEXTURES=3;'
if old not in text:
    raise SystemExit('Expected texture cache limits not found')
path.write_text(text.replace(old, new, 1), encoding='utf-8')

subprocess.run(['git', 'config', 'user.name', 'github-actions[bot]'], check=True)
subprocess.run(['git', 'config', 'user.email', '41898282+github-actions[bot]@users.noreply.github.com'], check=True)
subprocess.run(['git', 'add', 'Client PSVitaAlive/source/ui/full_catalog_screen.cpp'], check=True)
subprocess.run(['git', 'rm', 'scripts/_patch_texture_pipeline.py'], check=True)
subprocess.run(['git', 'commit', '-m', 'Restore nine visible app textures'], check=True)
subprocess.run(['git', 'push'], check=True)
