"""基板のfirmware書込みとUSB consoleの操作.

USBだけで完結させる。DFU（system bootloader）が見えていればそのまま書き込み、firmwareが
動いていればconsoleの`dfu` commandでbootloaderへ移してから書き込む。書込みにはSTM32CubeProgrammerの
CLIを使う（環境変数STM32_PROGRAMMER_CLIで場所を変えられる）。

使い方:
  py -3 -X utf8 firmware/tools/fwctl.py ports                 # 見えているdeviceを表示
  py -3 -X utf8 firmware/tools/fwctl.py flash [hexファイル]     # 既定: firmware/build/target/bringup.hex
  py -3 -X utf8 firmware/tools/fwctl.py cmd id clk            # consoleでcommandを実行して表示（-t 秒で待ち時間）
  py -3 -X utf8 firmware/tools/fwctl.py check                 # bring-up確認（id/clk/regs/pps）を記録
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import serial
import serial.tools.list_ports

FW = Path(__file__).resolve().parents[1]
DEFAULT_HEX = FW / "build" / "target" / "bringup.hex"
RESULTS = FW / "test_results"
CLI = Path(
    os.environ.get(
        "STM32_PROGRAMMER_CLI",
        r"C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    )
)

# usb_descriptors.cの仮VID/PID（P-21で決めるまで）
CDC_VID = 0xCAFE
CDC_PID = 0x4001
PROMPT = "> "


def find_cdc() -> str | None:
    for p in serial.tools.list_ports.comports():
        if p.vid == CDC_VID and p.pid == CDC_PID:
            return p.device
    return None


def dfu_present() -> bool:
    out = subprocess.run([str(CLI), "-l", "usb"], capture_output=True, text=True, errors="replace").stdout
    return "DFU in FS Mode" in out or "Device ID" in out


def wait_for(cond, timeout_s: float, interval_s: float = 0.5):
    end = time.monotonic() + timeout_s
    while time.monotonic() < end:
        r = cond()
        if r:
            return r
        time.sleep(interval_s)
    return None


class Console:
    def __init__(self, port: str):
        self.ser = serial.Serial(port, 115200, timeout=0.1)
        self.ser.dtr = True  # tud_cdc_connected()はDTRを見る
        time.sleep(0.3)
        # 毎秒の状態表示が応答に混ざらないよう止める（接続時のbannerも読み捨てる）
        self.ser.write(b"log off\r")
        time.sleep(0.3)
        self.ser.reset_input_buffer()

    def close(self) -> None:
        try:
            self.ser.write(b"log on\r")
            time.sleep(0.1)
            self.ser.close()
        except serial.SerialException:
            pass  # reset/dfu commandの後はportが消えている

    def run(self, command: str, timeout_s: float = 3.0) -> str:
        self.ser.write((command + "\r").encode())
        buf = b""
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            try:
                buf += self.ser.read(256)
            except serial.SerialException:
                buf += "\n(USBが切断された。reset/dfu/fault commandなら想定どおり)".encode()
                break
            if buf.endswith(PROMPT.encode()):
                break
        text = buf.decode("utf-8", errors="replace")
        # 先頭のecho行と末尾のpromptを除く
        lines = text.replace("\r\n", "\n").split("\n")
        if lines and lines[0].strip() == command:
            lines = lines[1:]
        out = "\n".join(lines)
        if out.endswith(PROMPT):
            out = out[: -len(PROMPT)]
        return out.rstrip("\n")


def cmd_ports(_args) -> int:
    print(f"CDC console : {find_cdc() or '-'}")
    print(f"USB DFU     : {'present' if dfu_present() else '-'}")
    return 0


def cmd_flash(args) -> int:
    hex_path = Path(args.hex).resolve()
    if not hex_path.exists():
        print(f"not found: {hex_path}")
        return 2
    if not dfu_present():
        port = find_cdc()
        if port is None:
            print("CDC consoleもDFUも見えない。USBを抜き差しするか、SWD（TP1～TP5）で復旧する")
            return 1
        print(f"{port}: dfu commandでbootloaderへ移す")
        try:
            ser = serial.Serial(port, 115200, timeout=0.1)
            ser.dtr = True
            ser.write(b"dfu\r")
            time.sleep(0.3)
            ser.close()
        except serial.SerialException as e:
            print(f"{port}を開けない（terminalで開いたままなら閉じるか、terminalから dfu と入力する）: {e}")
        if not wait_for(dfu_present, 15):
            print("DFUが現れない")
            return 1
    print(f"書込み: {hex_path}")
    r = subprocess.run(
        [str(CLI), "-c", "port=USB1", "-w", str(hex_path), "-v", "-s", "0x08000000"],
        capture_output=True,
        text=True,
        errors="replace",
    )
    tail = [ln for ln in r.stdout.splitlines() if ln.strip() and "\ufffd" not in ln][-6:]
    print("\n".join(tail))
    if "Download verified successfully" not in r.stdout:
        print("書込みまたはverifyに失敗")
        return 1
    port = wait_for(find_cdc, 15)
    if port is None:
        print("書込み後にCDC consoleが現れない（LEDの状態を確認する）")
        return 1
    print(f"起動確認: {port}")
    return 0


def cmd_cmd(args) -> int:
    port = find_cdc()
    if port is None:
        print("CDC consoleが見えない")
        return 1
    con = Console(port)
    try:
        for c in args.commands:
            print(f"> {c}")
            print(con.run(c, timeout_s=args.timeout))
    finally:
        con.close()
    return 0


def cmd_check(_args) -> int:
    port = find_cdc()
    if port is None:
        print("CDC consoleが見えない")
        return 1
    con = Console(port)
    try:
        outputs = {c: con.run(c) for c in ("id", "clk", "regs", "pps", "lse")}
        time.sleep(2.2)  # 生成PPS・GNSS captureの進み具合を見るため2秒あけてもう一度
        outputs["pps (2 s後)"] = con.run("pps")
    finally:
        con.close()

    text = "\n\n".join(f"> {k}\n{v}" for k, v in outputs.items())
    print(text)
    uid = re.search(r"UID\s+([0-9A-F]+)", outputs["id"])
    build = re.search(r"build\s+(\S+)", outputs["id"])
    ng = re.search(r"result: (\d+) NG", outputs["regs"])
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = RESULTS / (uid.group(1) if uid else "unknown") / f"{stamp}_{build.group(1) if build else 'unknown'}"
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "bringup_check.txt").write_text(text + "\n", encoding="utf-8")
    print(f"\n記録: {out_dir / 'bringup_check.txt'}")
    if ng is None or ng.group(1) != "0":
        print("register監査にNGあり")
        return 1
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ports").set_defaults(fn=cmd_ports)
    p = sub.add_parser("flash")
    p.add_argument("hex", nargs="?", default=str(DEFAULT_HEX))
    p.set_defaults(fn=cmd_flash)
    p = sub.add_parser("cmd")
    p.add_argument("commands", nargs="+")
    p.add_argument("-t", "--timeout", type=float, default=5.0, help="1 commandの応答を待つ秒数")
    p.set_defaults(fn=cmd_cmd)
    sub.add_parser("check").set_defaults(fn=cmd_check)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
