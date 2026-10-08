#!/usr/bin/env python3
"""Generate a small synthetic stereo OGG archive; no game assets required."""
import argparse
from pathlib import Path
import subprocess
import tempfile
from pack_assets import pack_archive

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="pal-probe-") as temporary:
        source = Path(temporary)
        (source / "ogg").mkdir()
        subprocess.run(["ffmpeg", "-nostdin", "-loglevel", "error", "-f", "lavfi", "-i",
            "aevalsrc=0.15*sin(2*PI*440*t)|0.15*sin(2*PI*660*t):s=48000:d=4",
            "-c:a", "libvorbis", "-q:a", "4", str(source / "ogg/001.ogg")], check=True)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        pack_archive(source, args.output)
