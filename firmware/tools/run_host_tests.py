"""host単体試験（L1）をbuildして実行する.

make/ninjaがない環境でも動くよう、gccを直接呼ぶ。core/src/*.c と tests/host/test_*.c を
test fileごとに1つの実行ファイルへbuildし、順に実行する。警告はerrorとして扱う。

加えて、test_time の --dump-rollover 出力（2000～2099年の全日の23:59:59と+1秒後）を
Pythonのdatetimeと照合する（firmware/test_plan.md UT-07の独立実装による照合）。

使い方:  py -3 -X utf8 firmware/tools/run_host_tests.py [-k 名前の一部] [--cc gcc]
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import shutil
import subprocess
import sys
from pathlib import Path

FW = Path(__file__).resolve().parents[1]
CORE_INC = FW / "core" / "include"
CORE_SRC = FW / "core" / "src"
TEST_DIR = FW / "tests" / "host"
BUILD = FW / "build" / "host"

CFLAGS = [
    "-std=c11",
    "-O1",
    "-g",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Wshadow",
    "-Wstrict-prototypes",
    "-Wmissing-prototypes",
    "-Werror",
]


def build(cc: str, test_src: Path) -> Path:
    exe = BUILD / (test_src.stem + (".exe" if os.name == "nt" else ""))
    cmd = [
        cc,
        *CFLAGS,
        f"-I{CORE_INC}",
        f"-I{TEST_DIR}",
        *map(str, sorted(CORE_SRC.glob("*.c"))),
        str(test_src),
        "-o",
        str(exe),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(f"BUILD FAILED: {test_src.name}")
    return exe


def check_rollover_against_datetime(exe: Path) -> bool:
    """test_timeが出力した日付繰上げをPythonのdatetimeと照合する."""
    out = subprocess.run([str(exe), "--dump-rollover"], capture_output=True, text=True, check=True).stdout
    lines = out.splitlines()
    day = dt.datetime(2000, 1, 1, 23, 59, 59)
    last = dt.datetime(2099, 12, 31, 23, 59, 59)
    expected = []
    while day <= last:
        nxt = day + dt.timedelta(seconds=1)
        expected.append(f"{day:%Y-%m-%d %H:%M:%S} {nxt:%Y-%m-%d %H:%M:%S}")
        day += dt.timedelta(days=1)
    if lines != expected:
        for i, (a, e) in enumerate(zip(lines, expected)):
            if a != e:
                print(f"rollover mismatch line {i}: got '{a}', expected '{e}'")
                break
        print(f"rollover lines: got {len(lines)}, expected {len(expected)}")
        return False
    print(f"PASS rollover_vs_python_datetime ({len(lines)} days)")
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-k", default="", help="test file名にこの文字列を含むものだけ実行する")
    ap.add_argument("--cc", default=os.environ.get("CC", "gcc"), help="host C compiler（既定: gcc）")
    args = ap.parse_args()

    if shutil.which(args.cc) is None:
        print(f"compiler not found: {args.cc}")
        return 2
    BUILD.mkdir(parents=True, exist_ok=True)

    failed: list[str] = []
    for src in sorted(TEST_DIR.glob("test_*.c")):
        if args.k not in src.stem:
            continue
        exe = build(args.cc, src)
        print(f"== {src.stem}")
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(result.stdout)
        sys.stdout.write(result.stderr)
        if result.returncode != 0:
            failed.append(src.stem)
        if src.stem == "test_time" and not check_rollover_against_datetime(exe):
            failed.append("rollover_vs_python_datetime")

    print()
    if failed:
        print("FAILED: " + ", ".join(failed))
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
