#!/usr/bin/env python3
"""Generate offline timer announcements; credentials stay on this computer."""
import argparse
import array
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request

import serial

if not sys.stdout.isatty() and hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

DEFAULT_CONFIG = Path(r"C:\Users\Administrator\WorkBuddy\2026-08-18-21-41-28\quiz-platform\site\tts_config.json")
DEFAULT_VOICE = "5ba3c68f-7d4d-4e42-be1c-90690664d00c"
DEFAULT_FFMPEG = Path(r"C:\Users\Administrator\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe\ffmpeg-9.0.2-full_build\bin\ffmpeg.exe")
MAX_BYTES = 44100 * 4 * 48 // 10


def valid_pcm(data):
    if not data or len(data) > MAX_BYTES or len(data) % 4:
        return False
    samples = array.array("h")
    samples.frombytes(data)
    return any(abs(sample) > 64 for sample in samples)


def voice_key(project, step):
    value = 14695981039346656037
    for b in project.encode("utf-8") + b"\0" + step.encode("utf-8"):
        value = ((value ^ b) * 1099511628211) & 0xffffffffffffffff
    return f"{value:016x}"


def get_pairs(port):
    device = serial.Serial(port=None, baudrate=115200, timeout=1)
    device.port = port
    device.dtr = False
    device.rts = False
    with device:
        time.sleep(0.3)
        device.reset_input_buffer()
        device.write(b"voice-list\n")
        deadline = time.monotonic() + 30
        started = False
        pairs = set()
        while time.monotonic() < deadline:
            line = device.readline().decode("ascii", "replace").strip()
            if line == "VOICE_LIST_BEGIN 1":
                started = True
            elif started and line == "VOICE_LIST_END":
                return sorted(pairs)
            elif started and line.startswith("VOICE "):
                parts = line.split(" ")
                if len(parts) != 3:
                    raise ValueError("板子返回的名称清单格式不完整")
                pairs.add((bytes.fromhex(parts[1]).decode("utf-8"), bytes.fromhex(parts[2]).decode("utf-8")))
        raise TimeoutError("未收到完整清单：请确认板子已连接且已安装支持语音的固件")


def generate_mp3(text, key, voice, proxy):
    body = json.dumps({"reference_id": voice, "text": text}, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request("https://fishaudio.org/api/open/tts", data=body, method="POST", headers={
        "Authorization": "Bearer " + key, "Content-Type": "application/json", "Accept": "audio/mpeg", "User-Agent": "Mozilla/5.0"
    })
    for selected_proxy in ([proxy] if proxy else ["", "http://127.0.0.1:12450"]):
        handlers = [urllib.request.ProxyHandler({"http": selected_proxy, "https": selected_proxy})] if selected_proxy else [urllib.request.ProxyHandler({})]
        try:
            with urllib.request.build_opener(*handlers).open(request, timeout=90) as response:
                if not response.headers.get("Content-Type", "").lower().startswith("audio/"):
                    raise ValueError("语音服务返回的不是音频")
                data = response.read(8 * 1024 * 1024 + 1)
                if len(data) > 8 * 1024 * 1024:
                    raise ValueError("语音文件超出大小限制")
                return data
        except urllib.error.HTTPError as error:
            raise RuntimeError(f"语音服务请求失败（HTTP {error.code}）") from None
        except urllib.error.URLError:
            continue
    raise RuntimeError("无法连接语音服务，请检查网络和代理")


def convert_mp3(ffmpeg, data, speed=1.0):
    command = [str(ffmpeg), "-hide_banner", "-loglevel", "error", "-i", "pipe:0"]
    if speed != 1.0:
        command += ["-filter:a", f"atempo={speed:.4f}"]
    command += ["-f", "s16le", "-acodec", "pcm_s16le", "-ar", "44100", "-ac", "2", "pipe:1"]
    result = subprocess.run(command, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        raise RuntimeError("音频转换失败，请检查 ffmpeg 和语音服务响应")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description="从板子读取试验与步骤，为 TF 卡生成离线语音")
    parser.add_argument("--port", default="COM5")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--voice", default=DEFAULT_VOICE)
    parser.add_argument("--ffmpeg", default=str(DEFAULT_FFMPEG) if DEFAULT_FFMPEG.exists() else (shutil.which("ffmpeg") or ""))
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[1] / "voice-pack" / "voice")
    parser.add_argument("--list-only", action="store_true")
    parser.add_argument("--force", action="store_true", help="重新生成已有音频")
    args = parser.parse_args()
    pairs = get_pairs(args.port)
    names_by_key = {}
    for pair in pairs:
        key_name = voice_key(*pair)
        if key_name in names_by_key and names_by_key[key_name] != pair:
            raise RuntimeError("发现文件名冲突，已停止生成")
        names_by_key[key_name] = pair
    print(f"读取到 {len(pairs)} 组试验与步骤")
    for project, step in pairs:
        print(f"  {project} / {step}")
    if args.list_only or not pairs:
        return
    if not args.ffmpeg or not Path(args.ffmpeg).is_file():
        raise RuntimeError("找不到 ffmpeg，请通过 --ffmpeg 指定可执行文件")
    with args.config.open(encoding="utf-8") as f:
        config = json.load(f)
    key = config.get("api_key", "")
    if not key:
        raise RuntimeError("语音配置中没有 API key")
    args.out.mkdir(parents=True, exist_ok=True)
    for index, (project, step) in enumerate(pairs, 1):
        destination = args.out / (voice_key(project, step) + ".pcm")
        if not args.force and destination.is_file() and valid_pcm(destination.read_bytes()):
            print(f"[{index}/{len(pairs)}] 已有：{project} / {step}")
            continue
        text = f"{project}，{step}，时间到了。"
        print(f"[{index}/{len(pairs)}] 生成：{text}", flush=True)
        mp3 = generate_mp3(text, key, args.voice, config.get("proxy", ""))
        pcm = convert_mp3(args.ffmpeg, mp3)
        if len(pcm) > MAX_BYTES:
            speed = max(1.05, len(pcm) / (MAX_BYTES * .95))
            if speed > 2.0:
                raise RuntimeError(f"语音超过 5 秒，请缩短名称：{project} / {step}")
            pcm = convert_mp3(args.ffmpeg, mp3, speed)
        if not valid_pcm(pcm):
            raise RuntimeError(f"语音长度或格式不合要求：{project} / {step}")
        temp = destination.with_suffix(".tmp")
        temp.write_bytes(pcm)
        os.replace(temp, destination)
    manifest = [{"project": project, "step": step, "file": voice_key(project, step) + ".pcm",
                 "seconds": round((args.out / (voice_key(project, step) + ".pcm")).stat().st_size / 176400, 2)} for project, step in pairs]
    (args.out.parent / "manifest.json").write_text(json.dumps({"voice": args.voice, "items": manifest}, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"完成。请将 {args.out} 整个 voice 文件夹复制到 FAT32 TF 卡根目录。")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, TimeoutError, serial.SerialException) as error:
        print(f"失败：{error}", file=sys.stderr)
        sys.exit(1)
