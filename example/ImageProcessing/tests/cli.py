import subprocess
import sys
from pathlib import Path
import ii_file_provider as fp

tool, resources = sys.argv[1:]
with fp.TemporaryDirectory(prefix="camera-image-", dir=resources) as temporary:
    output = Path(temporary) / "developed-gray.ppm"
    source = "color=c=0x404040:size=16x16:rate=10:duration=0.2"
    first = subprocess.run([tool, "lavfi", source, str(output), "srgb", "1"],
                           capture_output=True, text=True, timeout=15)
    if first.returncode:
        raise SystemExit("Photo processing CLI failed: " + first.stderr)
    data = output.read_bytes()
    if len(data) != 781 or not data.startswith(b"P6\n16 16\n255\n") or not 80 <= data[13] <= 95:
        raise SystemExit("Photo CLI produced an invalid image or exposure")
    second = subprocess.run([tool, "lavfi", "color=c=blue:size=16x16:rate=10:duration=0.2", str(output), "srgb"],
                            capture_output=True, text=True, timeout=15)
    if second.returncode == 0 or output.read_bytes() != data:
        raise SystemExit("Photo CLI overwrote its existing output")
