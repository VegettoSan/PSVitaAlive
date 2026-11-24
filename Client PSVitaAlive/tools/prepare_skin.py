"""Regenerate the approved UI atlases before packaging the Vita client."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from PIL import Image

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="psvitaalive-skin-") as work:
    work = Path(work)
    generator = work / "build_skin_assets.py"
    shutil.copyfile(root / "tools/build_skin_assets.py", generator)
    subprocess.run([sys.executable, str(generator)], check=True)
    atlas_dir = work / "atlases"
    coords = root / "include/ui/skin_atlas_coords.hpp"
    assert (atlas_dir / "atlas_coords.hpp").read_bytes() == coords.read_bytes(), "Atlas coordinates changed; review and update the checked-in header."
    target = root / "assets/ui/skin"
    target.mkdir(parents=True, exist_ok=True)
    for name, size in (("ui", 512), ("icons", 256), ("controls", 256)):
        filename = name + "_atlas.png"
        source = atlas_dir / filename
        with Image.open(source) as image:
            assert image.size == (size, size) and image.mode == "RGBA", filename
        shutil.copyfile(source, target / filename)
        print(f"Prepared {filename}: {size}x{size}")
