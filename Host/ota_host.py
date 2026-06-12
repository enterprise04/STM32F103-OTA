# -*- coding: utf-8 -*-
"""OTA 上位机:把固件 bin 经串口推送到 STM32(模拟云端服务器)

用法:
    python ota_host.py COM5 ..\\App\\MDK-ARM\\App\\App.bin --version 1.1

协议见 Common/ota.h:
    帧:  AA 55 | CMD | SEQ | LEN(2,LE) | DATA | CRC32(4,LE)   CRC 覆盖 CMD..DATA
    应答: 55 AA | STATUS | SEQ
"""
import argparse
import struct
import sys
import time
import zlib

try:
    import serial
except ImportError:
    sys.exit("缺少 pyserial,先执行: pip install pyserial")

CMD_START, CMD_DATA, CMD_END = 0x01, 0x02, 0x03
ST_NAMES = {
    0x00: "OK", 0x01: "帧CRC错", 0x02: "序号错",
    0x03: "状态/参数非法", 0x04: "Flash操作失败", 0x05: "固件CRC校验失败",
}
CHUNK = 1024
RETRY = 5

# 固件信息块魔数,与 App/Core/Inc/app_version.h 保持一致('FWVR' + 'INFO')
FW_INFO_MAGIC = struct.pack("<II", 0x52565746, 0x4F464E49)


def embedded_version(fw: bytes):
    """在 bin 中扫描固件信息块,返回 version 字;找不到返回 None"""
    idx = fw.find(FW_INFO_MAGIC)
    if idx < 0 or idx + 12 > len(fw):
        return None
    return struct.unpack_from("<I", fw, idx + 8)[0]


def build_frame(cmd: int, seq: int, data: bytes = b"") -> bytes:
    body = bytes([cmd, seq]) + struct.pack("<H", len(data)) + data
    return b"\xAA\x55" + body + struct.pack("<I", zlib.crc32(body))


def wait_ack(ser: serial.Serial, seq: int, timeout: float):
    """在字节流中同步 55 AA 应答头,返回 STATUS;超时返回 None"""
    deadline = time.time() + timeout
    window = b""
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        window = (window + b)[-4:]
        if len(window) == 4 and window[0] == 0x55 and window[1] == 0xAA and window[3] == seq:
            return window[2]
    return None


def send_frame(ser, cmd, seq, data=b"", timeout=2.0) -> bool:
    for attempt in range(1, RETRY + 1):
        ser.write(build_frame(cmd, seq, data))
        st = wait_ack(ser, seq, timeout)
        if st == 0x00:
            return True
        reason = "应答超时" if st is None else ST_NAMES.get(st, f"未知错误0x{st:02X}")
        print(f"\n  [重试 {attempt}/{RETRY}] cmd=0x{cmd:02X} seq={seq}: {reason}")
    return False


def main():
    ap = argparse.ArgumentParser(description="STM32 OTA 串口上位机")
    ap.add_argument("port", help="串口号,如 COM5")
    ap.add_argument("binfile", help="固件 bin 路径")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--version", default=None,
                    help="固件版本(后备)。bin 内嵌版本信息块优先,通常无需此参数")
    args = ap.parse_args()

    with open(args.binfile, "rb") as f:
        fw = f.read()
    if not fw:
        sys.exit("固件文件为空")
    fw_crc = zlib.crc32(fw)

    version = embedded_version(fw)
    if version is not None:
        ver_src = "bin 内嵌"
        if args.version:
            print("提示: bin 内含版本信息块,--version 参数被忽略")
    elif args.version:
        try:
            parts = (args.version.split(".") + ["0"])[:2]
            major, minor = int(parts[0] or 0), int(parts[1] or 0)
        except ValueError:
            sys.exit(f"--version 格式错误: '{args.version}',应为 主版本.次版本,如 1.1")
        version = (major << 16) | minor
        ver_src = "命令行"
    else:
        sys.exit("bin 中未找到版本信息块,请确认固件包含 g_fw_info,或用 --version 指定")

    print(f"固件: {args.binfile}")
    print(f"大小: {len(fw)} 字节, CRC32: 0x{fw_crc:08X}, "
          f"版本: v{version >> 16}.{version & 0xFFFF}({ver_src})")

    ser = serial.Serial(args.port, args.baud, timeout=0.05)
    ser.reset_input_buffer()        # 丢弃板端开机的调试打印

    # 1. START:固件大小 + 整包CRC + 版本
    print("握手 ...", end="", flush=True)
    meta = struct.pack("<III", len(fw), fw_crc, version)
    if not send_frame(ser, CMD_START, 0, meta):
        sys.exit("\n握手失败:检查板子是否在运行 App、串口助手是否占用了串口")
    print(" OK")

    # 2. DATA:1024 字节/包(末包可不足),序号从 1 起
    t0 = time.time()
    total = (len(fw) + CHUNK - 1) // CHUNK
    for i in range(total):
        chunk = fw[i * CHUNK:(i + 1) * CHUNK]
        seq = (i + 1) & 0xFF
        if not send_frame(ser, CMD_DATA, seq, chunk):
            sys.exit(f"\n第 {i + 1}/{total} 包发送失败,升级中止")
        done = (i + 1) * 100 // total
        print(f"\r发送 [{('#' * (done // 5)).ljust(20)}] {done:3d}%  ({i + 1}/{total} 包)",
              end="", flush=True)

    # 3. END:板端校验整包 CRC 并写参数区(给长一点超时)
    print("\n等待板端校验 ...", end="", flush=True)
    seq_end = (total + 1) & 0xFF
    if not send_frame(ser, CMD_END, seq_end, timeout=5.0):
        sys.exit("\n整包校验失败")
    print(" OK")

    print(f"升级包推送完成,耗时 {time.time() - t0:.1f}s。板端已写入升级标志,复位后生效。")
    ser.close()


if __name__ == "__main__":
    main()
