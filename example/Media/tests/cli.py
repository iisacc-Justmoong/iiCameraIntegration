import subprocess
import sys
from pathlib import Path
import ii_file_provider as fp

tool, resources = sys.argv[1:]
with fp.TemporaryDirectory(prefix="camera-cli-", dir=resources) as temporary:
    output = Path(temporary) / "frame.ppm"
    first = subprocess.run([tool, "--frame", "lavfi", "color=c=red:size=16x16:rate=10:duration=0.2", str(output)], capture_output=True, timeout=15)
    if first.returncode:
        raise SystemExit("Frame CLI failed: " + first.stderr.decode(errors="replace"))
    data = output.read_bytes()
    if len(data) != 781 or not data.startswith(b"P6\n16 16\n255\n") or not (data[13] > 200 and data[14] < 40 and data[15] < 40):
        raise SystemExit("Frame CLI did not produce red RGB24 pixels")
    second = subprocess.run([tool, "--frame", "lavfi", "color=c=blue:size=16x16:rate=10:duration=0.2", str(output)], capture_output=True, timeout=15)
    if second.returncode == 0 or output.read_bytes() != data:
        raise SystemExit("Frame CLI overwrote an existing output")
    probe = subprocess.run([tool, "--probe", str(output)], capture_output=True, text=True, timeout=15)
    if probe.returncode or "codec=ppm" not in probe.stdout or "dimensions=16x16" not in probe.stdout:
        raise SystemExit("Frame was not recognized by the media probe")
