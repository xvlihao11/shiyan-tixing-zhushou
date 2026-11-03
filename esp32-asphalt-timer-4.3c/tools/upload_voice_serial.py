#!/usr/bin/env python3
"""Copy known PCM voice clips to the board over its USB serial connection."""
import argparse
import binascii
import pathlib
import re
import time
import serial


def response(device, timeout=30):
    deadline = time.monotonic() + timeout
    ignored = []
    while time.monotonic() < deadline:
        line = device.readline().decode('ascii', errors='replace').strip()
        match = re.search(r'VOICE_(?:READY|ACK|DONE|ERROR|EXISTS) [^\r\n]+', line)
        if match:
            return match.group(0)
        if line:
            ignored.append(line)
            ignored = ignored[-3:]
    raise TimeoutError('板子没有在规定时间内响应，最近输出：' + repr(ignored))


def main():
    parser = argparse.ArgumentParser(description='通过 USB 将语音复制到板子的 TF 卡')
    parser.add_argument('--port', default='COM5')
    parser.add_argument('--source', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1] / 'voice-pack' / 'voice')
    args = parser.parse_args()
    clips = sorted(args.source.glob('*.pcm'))
    if not clips:
        raise ValueError('语音文件夹为空')
    device = serial.Serial(port=None, baudrate=115200, timeout=1, write_timeout=30)
    device.port = args.port
    device.dtr = False
    device.rts = False
    with device:
        time.sleep(0.5)
        device.reset_input_buffer()
        device.write(b'\n')
        time.sleep(0.2)
        device.reset_input_buffer()
        for index, path in enumerate(clips, 1):
            if not re.fullmatch(r'[0-9a-f]{16}', path.stem):
                raise ValueError(f'文件名无效: {path.name}')
            data = path.read_bytes()
            if not data or len(data) > 846720 or len(data) % 4:
                raise ValueError(f'音频格式或长度无效: {path.name}')
            crc = binascii.crc32(data) & 0xffffffff
            for attempt in range(1, 7):
                device.write(f'voice-put {path.stem} {len(data)} {crc:08x}\n'.encode('ascii'))
                reply = response(device)
                if reply.startswith('VOICE_EXISTS '):
                    parts = reply.split()
                    if len(parts) != 3 or int(parts[1]) != len(data) or int(parts[2], 16) != crc:
                        raise ValueError(f'TF 卡已有不同内容的文件，未覆盖: {path.name}')
                    print(f'{index}/{len(clips)} 已存在且校验一致: {path.name}', flush=True)
                    break
                if reply != 'VOICE_READY 128':
                    raise RuntimeError(f'{path.name}: {reply}')
                for offset in range(0, len(data), 128):
                    chunk = data[offset:offset + 128]
                    device.write(chunk)
                    received = offset + len(chunk)
                    expected = f'VOICE_DONE {received}' if received == len(data) else f'VOICE_ACK {received}'
                    reply = response(device, timeout=120 if received == len(data) else 30)
                    while reply.startswith('VOICE_ACK ') and reply != expected:
                        previous = int(reply.split()[1])
                        if previous >= received:
                            raise RuntimeError(f'{path.name}: 收到意外确认 {reply}，期望 {expected}')
                        reply = response(device)
                    if reply == 'VOICE_ERROR timeout':
                        print(f'{path.name} 传输超时，重试 {attempt}/6', flush=True)
                        device.write(b'\n')
                        time.sleep(0.3)
                        device.reset_input_buffer()
                        break
                    if reply != expected:
                        raise RuntimeError(f'{path.name}: 期望 {expected}，收到 {reply}')
                else:
                    print(f'{index}/{len(clips)} 已传输并校验: {path.name}', flush=True)
                    break
            else:
                raise RuntimeError(f'{path.name} 多次超时，已停止传输')


if __name__ == '__main__':
    main()
