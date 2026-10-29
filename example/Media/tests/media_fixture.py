import subprocess
import sys
from pathlib import Path
import ii_file_provider as fp

ffmpeg, test, resources = sys.argv[1:]
with fp.TemporaryDirectory(prefix="camera-media-", dir=resources) as temporary:
    root = Path(temporary)
    fixture = root / "capture.mkv"
    result = subprocess.run([ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
        "-f", "lavfi", "-i", "testsrc=size=64x48:rate=10", "-f", "lavfi",
        "-i", "sine=frequency=440:sample_rate=8000", "-t", "0.6", "-c:v", "ffv1",
        "-c:a", "pcm_s16le", "-f", "matroska", "pipe:1"], capture_output=True, timeout=30)
    if result.returncode:
        raise SystemExit("Synthetic FFmpeg fixture failed: " + result.stderr.decode(errors="replace"))
    fp.write_bytes(fixture, result.stdout)
    test_result = subprocess.run([test, str(fixture), str(root)], timeout=60)
    if test_result.returncode:
        raise SystemExit(test_result.returncode)
