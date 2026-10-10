"""GNSS受信試験: 製品firmwareのconsoleの毎秒logを記録し、lockまでの経過と位相差を表示する.

基板（USB）を先に給電し、UM982を後から給電する（pin_assignment「試作機の電源順序制約」）。
起動時と--snap秒ごとに`status`と`gnss`も記録する。Ctrl+Cで終えると要約を表示し、記録の末尾にも書く。
USBが一度切れても、consoleが戻れば自動でつなぎ直す。実行中はPCのsleepと画面消灯を止める
（蓋を閉じたときのsleepは止められない）。

使い方:
  py -3 -X utf8 firmware/tools/gnss_watch.py             # consoleを自動で探す
  py -3 -X utf8 firmware/tools/gnss_watch.py --snap 30   # statusとgnssを30秒ごと（既定60秒）
  py -3 -X utf8 firmware/tools/gnss_watch.py --debug     # gnss debug onで、受信した文・PPS・RMCの扱いも画面に出す

記録: firmware/test_results/<UID>/<YYYYMMDD>/gnss_watch_<HHMMSS>.txt（各行の先頭にPCの時刻）。
受信したbyteは%TEMP%\\cub2026_com.logへも写す。
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import statistics
import sys
import tempfile
import time
from pathlib import Path

import serial

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fwctl  # noqa: E402

NS_PER_COUNT = 1e9 / 48e6
MIRROR = Path(os.environ.get("TEMP", tempfile.gettempdir())) / "cub2026_com.log"

# timesync_log_line()の1行
LOG_RE = re.compile(
    r"\[(?P<state>[A-Z_]+)\] (?P<utc>time invalid|\S+) out=(?P<out>\w+) GNSS-PPS=(?P<pps>none|[+-]\d+) ?(?:cnt)? "
    r"RMC=(?P<rmc>\S) F=(?P<f>[+-][\d.]+) ppm good=(?P<good>\d+) \((?P<ev>.*)\)"
)

# 経過がこの秒数を超えても次へ進まないときに一度だけ出す助言
HINTS = [
    ("no_rmc", 60, "60秒たってもRMCが来ない。`gnss`の受信byteが0なら、UM982の電源・J2へつないだUART・出力設定・ハーネスを確かめる（OI-017）。--debugで毎秒の受信byte数と受信した文が見える"),
    ("no_fix", 180, "RMCは来ているがstatusがVのまま。空が開けた場所か、antennaの接続を確かめる（cold startは1分前後かかる）"),
    ("no_pps", 30, "RMC=Aなのに基板がGNSS PPSを捉えていない。UM982のPPS出力（CONFIG PPS）とJ2 pin 2を確かめる"),
    ("no_good", 30, "PPSとRMC=Aがあるのにgoodが増えない。RMCが1秒に1文（1 Hz、時刻の小数部.00）か、`gnss`のPPS→RMCの遅れが990 ms以内かを確かめる"),
]


class Tracker:
    def __init__(self) -> None:
        self.t0 = time.monotonic()
        self.first: dict[str, float] = {}
        self.transitions: list[tuple[float, str, str]] = []
        self.state: str | None = None
        self.locked_err: list[int] = []
        self.last: dict[str, str] = {}
        self.hinted: set[str] = set()
        self.last_print = 0.0

    def elapsed(self) -> float:
        return time.monotonic() - self.t0

    def mark(self, key: str, label: str) -> None:
        if key not in self.first:
            self.first[key] = self.elapsed()
            say(f"{fmt(self.first[key])} {label}")

    def feed(self, m: re.Match) -> None:
        g = m.groupdict()
        self.last = g
        now = self.elapsed()
        if g["rmc"] != "-":
            self.mark("rmc", f"RMCを受信（status {g['rmc']}）")
        if g["rmc"] == "A":
            self.mark("fix", "RMC=A（測位した）")
        if g["pps"] != "none":
            self.mark("pps", f"GNSS PPSを捕捉（位相差 {g['pps']} count）")
        if g["state"] != self.state:
            self.transitions.append((now, self.state or "-", g["state"]))
            say(f"{fmt(now)} 状態 {self.state or '-'} → {g['state']}（{g['ev']}）")
            self.state = g["state"]
            if g["state"] == "GNSS_LOCKED":
                self.mark("lock", "GNSS_LOCKED")
        if g["state"] == "GNSS_LOCKED" and g["pps"] != "none":
            self.locked_err.append(int(g["pps"]))
        self.hint(now, g)
        if now - self.last_print >= 10:
            self.last_print = now
            say(
                f"{fmt(now)} [{g['state']}] {g['utc']} out={g['out']} PPS={g['pps']} RMC={g['rmc']} "
                f"F={g['f']} ppm good={g['good']}{self.err_text(short=True)}"
            )

    def hint(self, now: float, g: dict) -> None:
        cond = {
            "no_rmc": "rmc" not in self.first and now > HINTS[0][1],
            "no_fix": "rmc" in self.first and "fix" not in self.first and now - self.first["rmc"] > HINTS[1][1],
            "no_pps": "fix" in self.first and "pps" not in self.first and now - self.first["fix"] > HINTS[2][1],
            "no_good": "pps" in self.first
            and "fix" in self.first
            and "lock" not in self.first
            and g["good"] == "0"
            and now - max(self.first["pps"], self.first["fix"]) > HINTS[3][1],
        }
        for key, _, text in HINTS:
            if cond[key] and key not in self.hinted:
                self.hinted.add(key)
                say(f"{fmt(now)} 注意: {text}")

    def err_text(self, short: bool = False) -> str:
        e = self.locked_err[-600:] if short else self.locked_err
        if not e:
            return ""
        mx = max(abs(v) for v in e)
        if short:
            return f" lock後|err|max={mx} count（{mx * NS_PER_COUNT:.0f} ns、直近{len(e)}秒）"
        sd = statistics.pstdev(e) if len(e) > 1 else 0.0
        return (
            f"lock中の位相差 {len(e)}秒: 平均 {statistics.mean(e):+.2f} count、σ {sd:.2f} count（{sd * NS_PER_COUNT:.0f} ns）、"
            f"最大|err| {mx} count（{mx * NS_PER_COUNT:.0f} ns）。1 count = 20.8 ns、合否の目安はlock判定と同じ48 count（1 us）"
        )

    def summary(self) -> str:
        lines = ["== 要約", f"記録時間 {fmt(self.elapsed())}"]
        names = [("rmc", "最初のRMC"), ("fix", "最初のRMC=A"), ("pps", "最初のGNSS PPS"), ("lock", "GNSS_LOCKED")]
        for key, name in names:
            lines.append(f"{name:<16}{fmt(self.first[key]) if key in self.first else 'なし'}")
        if "lock" in self.first and "pps" in self.first and "fix" in self.first:
            ready = max(self.first["pps"], self.first["fix"])
            lines.append(f"PPSとRMC=Aがそろってからlockまで {self.first['lock'] - ready:.0f}秒（PP-05の目安5～6秒、stepした場合は数秒長い）")
        lines.append("状態遷移:")
        lines += [f"  {fmt(t)} {a} → {b}" for t, a, b in self.transitions]
        if self.last:
            lines.append(f"最後のF {self.last['f']} ppm（TCXOの偏差。GNSS_LOCKED中ならGNSSで測った値）")
        if self.locked_err:
            lines.append(self.err_text())
        return "\n".join(lines)


def fmt(sec: float) -> str:
    return f"{int(sec // 60):3d}:{int(sec % 60):02d}"


def say(text: str) -> None:
    print(text, flush=True)


def open_console(port: str) -> serial.Serial:
    s = serial.Serial(port, 115200, timeout=0.2)
    s.dtr = True  # tud_cdc_connected()はDTRを見る
    return s


def wait_console() -> serial.Serial:
    while True:
        port = fwctl.find_cdc()
        if port:
            try:
                s = open_console(port)
                time.sleep(0.3)
                return s
            except serial.SerialException as e:
                say(f"{port}を開けない（{e}）。Serial Monitorなどが開いていれば閉じる")
        time.sleep(1.0)


def read_uid(s: serial.Serial) -> tuple[str, bytes]:
    s.write(b"log off\r")
    time.sleep(0.3)
    raw = s.read(4096)
    s.write(b"id\r")
    end = time.monotonic() + 2.0
    while time.monotonic() < end:
        raw += s.read(512)
        if b"UID" in raw and raw.endswith(b"> "):
            break
    m = re.search(rb"UID\s+([0-9A-F]+)", raw)
    return (m.group(1).decode() if m else "unknown"), raw


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--snap", type=float, default=60.0, help="statusとgnssを記録する間隔（秒、0で起動時だけ）")
    ap.add_argument("--seconds", type=float, default=0.0, help="この秒数で終える（0ならCtrl+Cまで）")
    ap.add_argument("--debug", action="store_true", help="gnss debug onにして、その行（gnss …）も画面に出す")
    args = ap.parse_args()
    start_cmds = b"status\rgnss\rlog on\r" + (b"gnss debug on\r" if args.debug else b"")

    fwctl.keep_host_awake()
    say("consoleを探している…")
    ser = wait_console()
    uid, first_raw = read_uid(ser)
    start = dt.datetime.now()
    out_dir = fwctl.RESULTS / uid / start.strftime("%Y%m%d")
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"gnss_watch_{start:%H%M%S}.txt"
    say(f"UID {uid}、記録: {path}")
    say(f"受信は{MIRROR}へも写す。Ctrl+Cで終了")

    tracker = Tracker()
    pending = b""
    with open(path, "a", encoding="utf-8") as log, open(MIRROR, "ab") as mirror:

        def take(data: bytes) -> None:
            nonlocal pending
            mirror.write(data)
            mirror.flush()
            pending += data
            *done, pending = pending.split(b"\n")
            stamp = dt.datetime.now().strftime("%H:%M:%S.%f")[:-3]
            for raw in done:
                line = raw.decode("utf-8", errors="replace").replace("\r", "")
                if line.startswith("> "):
                    line = line[2:]
                if not line.strip():
                    continue
                log.write(f"{stamp} {line}\n")
                if args.debug and line.startswith("gnss "):
                    say(f"{fmt(tracker.elapsed())} {line}")
                m = LOG_RE.search(line)
                if m:
                    tracker.feed(m)
            log.flush()

        take(first_raw)
        ser.write(start_cmds)
        next_snap = time.monotonic() + args.snap if args.snap > 0 else float("inf")
        try:
            while args.seconds <= 0 or tracker.elapsed() < args.seconds:
                try:
                    data = ser.read(4096)
                    if data:
                        take(data)
                    if time.monotonic() >= next_snap:
                        next_snap += args.snap
                        ser.write(b"status\rgnss\r")
                except serial.SerialException:
                    say(f"{fmt(tracker.elapsed())} USBが切れた。consoleが戻るのを待つ")
                    log.write(f"{dt.datetime.now():%H:%M:%S} (USB切断)\n")
                    try:
                        ser.close()
                    except serial.SerialException:
                        pass
                    ser = wait_console()
                    say(f"{fmt(tracker.elapsed())} つなぎ直した")
                    ser.write(start_cmds)
        except KeyboardInterrupt:
            pass
        text = tracker.summary()
        say("\n" + text)
        log.write("\n" + text + "\n")
    try:
        ser.close()
    except serial.SerialException:
        pass
    say(f"\n記録: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
