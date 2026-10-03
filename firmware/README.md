# timing_sync_if firmware

U1 `STM32G0B1RCT6`で動作するfirmware。試験計画は[`test_plan.md`](./test_plan.md)、仕様の正本は[`../hardware/overall/`](../hardware/overall/)と[`../plan/design.md`](../plan/design.md)。

## 構成

| ディレクトリ | 内容 |
|---|---|
| [`core/`](./core/) | HAL/register非依存のロジック。hostでもtargetでも同じsourceをbuildする |
| [`tests/host/`](./tests/host/) | L1 host単体試験（test_plan.md §5.1 UT） |
| [`target/`](./target/) | STM32固有のstartup、linker script、clock/GPIO初期化、USB CDC console。現在はbring-up firmware |
| [`third_party/`](./third_party/) | 外部source（CMSIS、TinyUSBの必要部分だけ） |
| [`tools/`](./tools/) | 試験の実行、書込み、console操作のscript |

## bring-up firmware

全出力をHigh-Zに保ったまま、clock（TCXO→HSE bypass→PLL 48 MHz）、USB CDC console、TIM2（TP6へ1 Hz・100 msの生成PPS、PA5でGNSS PPSの立下りcapture）を動かす。外部コネクタへは何も出さない。

LEDの表示: 1秒に1回100 ms点灯=正常、200 ms周期の点滅=HSE（TCXO系）異常で診断mode、消灯=起動していない。

USB CDC（Windowsでは`COMx`、VID 0xCAFE / PID 0x4001）をTera TermやPuTTYで開くと、起動情報を表示した後、生成PPSの秒境界ごとに1行の状態（経過秒、clock、GNSS PPSのcapture数、TCXOに対するLSE偏差）を流す。baud rateの設定は無関係。

consoleのcommand: `help`、`id`（DEV_ID/REV_ID/UID/reset要因）、`clk`、`regs`（初期化registerの監査）、`pps`、`lse`（X1の起動とTCXOに対する偏差）、`gpio`（全I/O pinの設定照合）、`inputs`（U4出力の静的レベル＝外部入力のbiasと反転の確認）、`rxidle`（受信UART 4系統の未接続時の誤受信確認）、`log on`/`log off`（毎秒の状態表示）、`dfu`（USB DFUへ移る）、`reset`。`fwctl.py`はcommand実行中だけ`log off`にする。

### selftest image（test build）

`firmware/build/target/selftest.hex`はbring-upに試験用commandを加えたもの（約100 KB、full newlib）。使い終わったら`bringup.hex`へ戻す。

- `ut`: `tests/host/test_*.c`をそのままMCU上で実行する（UT-13、約30秒）。`fwctl.py cmd -t 180 ut`
- `fault css`: 次の起動をHSE CSSの後と同じclock診断mode（SYSCLK=HSI16、USB=HSI48+CRS）にする。`reset`で通常に戻る

### buildと書込み

ARM toolchainはMSYS2の`mingw-w64-x86_64-arm-none-eabi-toolchain`と`mingw-w64-x86_64-ninja`、書込みはSTM32CubeProgrammerを使う。

```sh
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S firmware/target -B firmware/build/target -G Ninja
cmake --build firmware/build/target

py -3 -X utf8 firmware/tools/fwctl.py flash   # DFUが見えていればそのまま、firmwareが動いていればdfu commandで移してから書く
py -3 -X utf8 firmware/tools/fwctl.py check   # id/clk/regs/ppsを実行し、firmware/test_results/<UID>/へ記録
py -3 -X utf8 firmware/tools/fwctl.py cmd id  # 任意のcommand
```

- 未書込みの基板はUSBをつなぐだけでDFUになる。書込み後はbootloaderのGOで起動し、firmwareが`EMPTY` bitを0にして一度resetし直すため、電源の入れ直しは要らない（RM0444ではEMPTYはsoftwareで書ける。[`pin_assignment.md`](../hardware/overall/pin_assignment.md)「初回bring-up手順」の「書込み後は電源を入れ直す」は、この処理がないfirmwareの場合の手順）。
- 出荷時のoption byte（`nBOOT_SEL=1`）ではSW2+SW1でDFUに入れない。firmwareが応答しなくなった場合はUSBを抜き差しし、それでも駄目ならSWD（TP1～TP5）で書き直す:

```sh
"/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe" -c port=SWD mode=UR -w firmware/build/target/bringup.hex -v -rst
```

ST-LINK V2のクローンを使う場合、STM32CubeProgrammerがST-LINKのfirmware更新を勧めても行わない（クローンは更新で使えなくなることがある）。

## core module

| module | 内容 | 試験 |
|---|---|---|
| [`ts_crc32c`](./core/include/ts_crc32c.h) | CRC32C（Jetson frame、backup payload） | UT-01 |
| [`ts_time`](./core/include/ts_time.h) | UTC暦日時⇔Unix秒、日付繰上げ、RTC範囲 | UT-07、UT-08 |
| [`ts_nmea`](./core/include/ts_nmea.h) | NMEA受信組立て、checksum検証、RMC解析・生成 | UT-05～UT-07 |
| [`ts_jetson_proto`](./core/include/ts_jetson_proto.h) | Jetson frameの受信（再同期・timeout）、送信、TIME_SET/TIME_STATUS/ACK、再送判定 | UT-02～UT-04 |

## host単体試験の実行

gccだけで動く（make/ninja不要）。

```sh
py -3 -X utf8 firmware/tools/run_host_tests.py        # 全部
py -3 -X utf8 firmware/tools/run_host_tests.py -k nmea  # 名前で絞る
```

`TODO`行は未決定の仕様（test_plan.md §2のP項目）を示すもので、失敗ではない。

## Jetson protocolのresult/error code

[`jetson_time_protocol.md`](../hardware/overall/jetson_time_protocol.md)はNACK codeと`last error`を「実装enum」としているため、[`ts_jetson_proto.h`](./core/include/ts_jetson_proto.h)の値を正とする。Jetson側の実装はこの表に合わせる。

| ACK/NACK code | 名前 |
|---:|---|
| `0x00` | OK |
| `0x01` | `NACK_GNSS_ACTIVE` |
| `0x02` | `NACK_OFFSET_TOO_LARGE` |
| `0x03` | `NACK_RTC_WRITE` |
| `0x04` | `NACK_EXPIRED` |
| `0x05` | `NACK_OUT_OF_RANGE`（nanoseconds、`valid_for_ms`、Unix秒の範囲外） |
| `0x06` | `NACK_BAD_LENGTH` |
| `0x07` | `NACK_UNKNOWN_COMMAND` |

`TIME_STATUS`の`last error`は、`0x0000`=none、`0x0001`～`0x00FF`=直近に返したNACKのcode、`0x0101`=frame CRC、`0x0102`=frame length、`0x0103`=frame version、`0x0104`=inter-byte timeout。
