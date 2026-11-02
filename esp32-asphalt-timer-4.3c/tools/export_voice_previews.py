from pathlib import Path
import json
import re
import subprocess

root = Path(__file__).resolve().parents[1] / "voice-pack"
manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
output = root / "电脑试听"
output.mkdir(exist_ok=True)
ffmpeg = Path(r"C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-9.0.2-full_build\bin\ffmpeg.exe")
for index, item in enumerate(manifest["items"], 1):
    name = re.sub(r'[\\/:*?"<>|]', "_", item["project"] + " - " + item["step"])
    dest = output / f"{index:02d} {name}.mp3"
    subprocess.run([str(ffmpeg), "-hide_banner", "-loglevel", "error", "-y", "-f", "s16le", "-ar", "44100", "-ac", "2", "-i", str(root / "voice" / item["file"]), "-b:a", "128k", str(dest)], check=True, capture_output=True)
print(f"Created {len(manifest['items'])} MP3 files in {output}")
