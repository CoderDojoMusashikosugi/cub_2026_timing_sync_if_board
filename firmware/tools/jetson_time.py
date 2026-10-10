"""Jetson時刻protocol（USB CDC）のclient. JetsonでもPCでも動く（pyserialだけ使う）.

仕様は hardware/overall/jetson_time_protocol.md、result codeは core/include/ts_jetson_proto.h。
製品firmwareはUSB CDCを2つ出し、2つ目（interface 2）がこのprotocol用。portは自動で探す
（Linuxでは /dev/serial/by-id/usb-cub2026_timing_sync_if_<UID>-if02 も使える）。

使い方:
  python3 jetson_time.py status            # GET_STATUS
  python3 jetson_time.py set               # このPC/Jetsonの時計でSET_TIME（GNSS有効中は拒否される）
  python3 jetson_time.py force-set         # FORCE_SET_TIME（出力を一度High-Zにしてstepする）
  python3 jetson_time.py watch [-n 秒]     # GET_STATUSを毎秒表示し、本機の時計との差を出す
  -p PORTでportを指定する
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
import time

import serial
import serial.tools.list_ports

VID, PID = 0xCAFE, 0x4001  # usb_descriptors.cの仮VID/PID（P-21）
MAGIC = b"TS"
VERSION = 1
CMD_GET_STATUS, CMD_SET_TIME, CMD_FORCE_SET_TIME = 0x01, 0x02, 0x03
CMD_STATUS, CMD_ACK, CMD_NACK = 0x81, 0x82, 0x83

STATES = ["INIT_INVALID", "GNSS_ACQUIRE", "GNSS_LOCKED", "JETSON_SET", "TCXO_HOLDOVER", "RESYNC", "FAULT",
          "FAULT_CLOCK"]
RESULTS = {0: "OK", 1: "NACK_GNSS_ACTIVE", 2: "NACK_OFFSET_TOO_LARGE", 3: "NACK_RTC_WRITE", 4: "NACK_EXPIRED",
           5: "NACK_OUT_OF_RANGE", 6: "NACK_BAD_LENGTH", 7: "NACK_UNKNOWN_COMMAND"}


def crc32c(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0x82F63B78 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF


def encode(command: int, sequence: int, payload: bytes = b"") -> bytes:
    head = MAGIC + struct.pack("<BBHH", VERSION, command, len(payload), sequence) + payload
    return head + struct.pack("<I", crc32c(head))


def find_port() -> str | None:
    for p in serial.tools.list_ports.comports():
        if p.vid == VID and p.pid == PID and ((p.location or "").endswith(".2") or "jetson" in (p.interface or "")):
            return p.device
    return None


class Client:
    def __init__(self, port: str):
        self.ser = serial.Serial(port, 115200, timeout=0.002)
        self.ser.dtr = True
        self.seq = int.from_bytes(os.urandom(2), "little") or 1

    def request(self, command: int, payload: bytes = b"", timeout: float = 1.0) -> tuple[int, bytes]:
        self.seq = (self.seq + 1) & 0xFFFF or 1
        self.ser.reset_input_buffer()
        self.ser.write(encode(command, self.seq, payload))
        buf = b""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            buf += self.ser.read(self.ser.in_waiting or 1)
            i = buf.find(MAGIC)
            if i < 0 or len(buf) < i + 8:
                continue
            _ver, cmd, length, seq = struct.unpack_from("<BBHH", buf, i + 2)
            total = 8 + length + 4
            if len(buf) < i + total:
                continue
            frame = buf[i:i + total]
            if struct.unpack_from("<I", frame, 8 + length)[0] != crc32c(frame[:8 + length]) or seq != self.seq:
                buf = buf[i + 1:]
                continue
            return cmd, frame[8:8 + length]
        raise TimeoutError("応答なし")

    def status(self) -> dict:
        cmd, p = self.request(CMD_GET_STATUS)
        if cmd != CMD_STATUS or len(p) != 26:
            raise RuntimeError(f"unexpected response 0x{cmd:02X} len {len(p)}")
        unix, ns, state, flags, _r, src, seq, _diag, err = struct.unpack("<qIBBHIHHH", p)
        return {"unix": unix, "ns": ns, "state": STATES[state] if state < len(STATES) else state, "flags": flags,
                "rtc_valid": bool(flags & 1), "gnss_valid": bool(flags & 2), "clock_fault": bool(flags & 4),
                "lse_ready": bool(flags & 16), "last_source_id": src, "last_sequence": seq, "last_error": err}

    def set_time(self, force: bool, source_id: int) -> str:
        now = time.time_ns()
        payload = struct.pack("<qIII", now // 1_000_000_000, now % 1_000_000_000, 500, source_id)
        # stepのACKは時刻の適用・RTC書込み・backup commitの後（数秒）に返る
        cmd, p = self.request(CMD_FORCE_SET_TIME if force else CMD_SET_TIME, payload, timeout=10.0)
        if cmd not in (CMD_ACK, CMD_NACK) or len(p) != 2:
            raise RuntimeError(f"unexpected response 0x{cmd:02X}")
        return RESULTS.get(p[1], f"0x{p[1]:02X}")


def fmt_status(s: dict, rtt_ms: float | None = None) -> str:
    t = time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(s["unix"])) + f".{s['ns'] // 1_000_000:03d}Z"
    text = (f"{s['state']:<13} {t if s['unix'] else 'time invalid':<24} gnss={int(s['gnss_valid'])} "
            f"rtc={int(s['rtc_valid'])} lse={int(s['lse_ready'])} clock_fault={int(s['clock_fault'])} "
            f"last_src=0x{s['last_source_id']:08X} "
            f"last_seq={s['last_sequence']} last_err=0x{s['last_error']:04X}")
    if rtt_ms is not None:
        text += f" rtt={rtt_ms:.1f}ms"
    return text


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["status", "set", "force-set", "watch"])
    ap.add_argument("-p", "--port")
    ap.add_argument("-n", type=int, default=0, help="watchの回数（0=無限）")
    args = ap.parse_args()
    port = args.port or find_port()
    if port is None:
        print("Jetson protocolのUSB CDC（interface 2）が見つからない")
        return 1
    c = Client(port)
    source_id = int.from_bytes(os.urandom(4), "little")
    if args.command == "status":
        print(fmt_status(c.status()))
    elif args.command in ("set", "force-set"):
        r = c.set_time(args.command == "force-set", source_id)
        print(f"{args.command}: {r}")
        return 0 if r == "OK" else 1
    else:
        i = 0
        while args.n == 0 or i < args.n:
            t0 = time.time()
            s = c.status()
            t1 = time.time()
            line = fmt_status(s, (t1 - t0) * 1000)
            if s["unix"]:
                board = s["unix"] + s["ns"] / 1e9
                line += f" board−host={(board - (t0 + t1) / 2) * 1000:+.1f}ms"
            print(line, flush=True)
            i += 1
            time.sleep(max(0.0, 1.0 - (time.time() - t0)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
