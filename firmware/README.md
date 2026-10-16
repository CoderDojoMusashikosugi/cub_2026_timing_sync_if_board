# timing_sync_if firmware

U1 `STM32G0B1RCT6`で動作するfirmware。試験計画は[`test_plan.md`](./test_plan.md)、仕様の正本は[`../hardware/overall/`](../hardware/overall/)と[`../plan/design.md`](../plan/design.md)。

## 構成

| ディレクトリ | 内容 |
|---|---|
| [`core/`](./core/) | HAL/register非依存のロジック。hostでもtargetでも同じsourceをbuildする |
| [`tests/host/`](./tests/host/) | L1 host単体試験（test_plan.md §5.1 UT） |
| [`target/`](./target/) | STM32固有のstartup、linker script、clock/GPIO初期化、USB CDC console。bring-up、selftest、製品firmware（app）の3つのimageをbuildする |
| [`third_party/`](./third_party/) | 外部source（CMSIS、TinyUSBの必要部分だけ） |
| [`tools/`](./tools/) | 試験の実行、書込み、console操作のscript |

## bring-up firmware

全出力をHigh-Zに保ったまま、clock（TCXO→HSE bypass→PLL 48 MHz）、USB CDC console、TIM2（TP6へ1 Hz・100 msの生成PPS、PA5でGNSS PPSの立下りcapture）を動かす。外部コネクタへは何も出さない。

LEDの表示: 1秒に1回100 ms点灯=正常、200 ms周期の点滅=HSE（TCXO系）異常で診断mode、消灯=起動していない。

USB CDC（Windowsでは`COMx`、VID 0xCAFE / PID 0x4001）をTera TermやPuTTYで開くと、起動情報を表示した後、生成PPSの秒境界ごとに1行の状態（経過秒、clock、GNSS PPSのcapture数、TCXOに対するLSE偏差）を流す。baud rateの設定は無関係。

consoleのcommand: `help`、`id`（DEV_ID/REV_ID/UID/reset要因）、`clk`、`regs`（初期化registerの監査）、`pps`、`lse`（X1の起動とTCXOに対する偏差）、`gpio`（全I/O pinの設定照合）、`inputs`（U4出力の静的レベル＝外部入力のbiasと反転の確認）、`rxidle`（受信UART 4系統の未接続時の誤受信確認）、`log on`/`log off`（毎秒の状態表示）、`dfu`（USB DFUへ移る）、`reset`。`fwctl.py`はcommand実行中だけ`log off`にする。

### 製品firmware（app image）

`firmware/build/target/app.hex`。GNSS（J2: UM982のPPSとRMC）で生成PPSを規律し、J3～J8の6系統へPPS（High 100 ms）とGPRMC（9600 baud、PPS立上りの200 ms後）を出す。J4（VLP）はTXINVで反転し、UTC+1秒にする。状態遷移は[`time_source_state_machine.md`](../hardware/overall/time_source_state_machine.md)、まだ暫定の値は[`test_plan.md`](./test_plan.md) §2.1。

- 起動直後は全出力High-Z。GNSSのPPSとstatus `A`のRMCがそろうと、まず生成PPSをGNSS PPSへstepで揃え、位相差1 us以内が5秒続くとlockして出力をenableする（屋外の実測でPPSとRMCがそろってから約8秒）
- GNSSを失うとholdover（PPS継続、RMC=`V`）。失ったと判断するのは、RMC=`V`が1回、PPSの欠落が2回続く（lock中は100 usより離れたPPSも欠落と数える）、RMCの無効・欠落が2回続く、のいずれか
- GNSSが5秒続けて正常に戻ったときの合わせ方は、holdover中の時刻の出どころで変わる。GNSS由来なら位相差100 ms以内はslew（周期の補正は±100 us/秒以内）で合わせ、PPSは止めない。100 ms超か秒が違えば`FAULT`（全出力High-Z。`reinit`かJetsonのSETで抜ける）。RTCまたはJetson由来の時刻で1 ms超か秒が違えば、出力をHigh-Zにしてstepし、2秒正常が続いてから出力を戻す
- USB CDCが2つ出る。interface 0がconsole（`fwctl.py`はこちらを使う）、interface 2がJetson protocol（[`tools/jetson_time.py`](./tools/jetson_time.py)）
- GNSSがない場所では、`jetson_time.py set`でPC/Jetsonの時計から時刻を与えると、holdover（RMC=`V`）で出力を始める
- 時刻は内蔵RTC（LSE）にも書き、CR2032があれば電源OFF中も保持する。起動時にbackup domainを検証し（signature、CRC、RTC設定、calendar範囲、SSRの進み）、合格すればRTCの秒境界へ生成PPSを揃え、RTCの秒が2回続けて一致してからholdover（RMC=`V`）で出力を始める。不合格ならBDRSTして時刻なしで起動する。software reset・DFU書込みでは電源が切れないので電池なしでも保持される

LEDの表示（app）: PPSに合わせて1秒に1回100 ms点灯=GNSSでlock中、1秒に2回点灯=holdover・step中・GNSSへの再同期中、200 ms周期の速い点滅=`FAULT`または`FAULT_CLOCK`、1秒に1回の短い点灯=時刻なし（`INIT_INVALID`）。

異常時の動作: HSE CSS（TCXO系のclock停止）を検出すると印を残してresetし、次の起動はHSIの診断mode（`FAULT_CLOCK`、全出力High-Z）になる。HardFaultとCSS以外のNMIでは、USBから書き直せるように自動でsystem bootloader（DFU）へ移る。

consoleのcommand（bring-upのcommandに加えて）: `status`（state、UTC、出力、DPLL、各TXの送信数、最後に送ったRMC）、`gnss`（受信baud、NMEA数、PPS→RMCの遅れ、位置。`gnss baud 115200`で固定、`gnss auto`で自動。`gnss debug on`で、checksumの合った文を直前のGNSS PPSからの経過付きで、GNSS PPSの位相差と間隔、各RMCを時刻に使ったかとその理由、捨てた文とその理由、baudの切替、毎秒の受信byte数とUART errorを逐一出す。`gnss debug off`で止める。試験用で、hostが読まないと1行ごとに最大5 ms待つ）、`out`（`out off`で全出力High-Z、`out on`で戻す、`out mask 3F`でTXを選ぶ）、`reinit`（`FAULT`などから`INIT_INVALID`へ戻す）。毎秒のlogは状態、UTC、GNSS PPSの位相差（count、1 count=20.8 ns）、RMC status、TCXO偏差の推定を出す。

```sh
py -3 -X utf8 firmware/tools/fwctl.py flash firmware/build/target/app.hex
py -3 -X utf8 firmware/tools/fwctl.py cmd status gnss
py -3 -X utf8 firmware/tools/jetson_time.py watch     # Jetson protocolで毎秒GET_STATUS
py -3 -X utf8 firmware/tools/gnss_watch.py            # GNSS受信試験: 毎秒logを記録し、lockまでの経過と位相差を表示
```

### selftest image（test build）

`firmware/build/target/selftest.hex`はbring-upに試験用commandを加えたもの（約125 KB、full newlib）。使い終わったら`bringup.hex`へ戻す。

- `ut`: `tests/host/test_*.c`をそのままMCU上で実行する（UT-13、約30秒）。`fwctl.py cmd -t 180 ut`
- `fault css`: 次の起動をHSE CSSの後と同じclock診断mode（SYSCLK=HSI16、USB=HSI48+CRS）にする。`reset`で通常に戻る
- `loop j3`（`j7`、`j8`）: 対象コネクタのpin 3とpin 4を短絡して実行する（UA-03、IO-03）。OE_N disableで折り返らないこと、enableでの静的レベルと極性、9600～3000000 baudの送受信一致を見る。`loop j3 115200 1000000`のようにbaudとbyte数を与えると、その1条件だけを長く行う（`fwctl.py cmd -t 150 "loop j3 115200 1000000"`）。試験中だけ対象channelのOE_Nを下げ、終わるとHigh-Zに戻す
- `loop j3j2`: J3 pin 1～3をJ2 pin 1～3へつないで実行する。J3 TX→J2 NMEA入力（USART3）で同じ試験を行う。`loop j3j2 pps`はUART試験の間も`PPS_OE_N`を下げて生成PPSをJ2 pin 2へ入れ、GNSS PPS captureに誤captureが出ないことも見る
- `ppsloop [秒]`: J3 pin 2→J2 pin 2で生成PPSをGNSS PPS capture（TIM2 CH1）へ折り返す。`PPS_OE_N` disableでcaptureがないこと、enableで毎秒1回、立上りからの遅れが一定であること、両edgeでHigh幅100 msを見る。`PPS_OE_N`はJ3～J8のpin 2を同時に出すので、J4～J8には何もつながない

### buildと書込み

buildにはCMake 3.20以上、Ninja、arm-none-eabi-gccが要る。ARM toolchainはMSYS2の`mingw-w64-x86_64-arm-none-eabi-toolchain`と`mingw-w64-x86_64-ninja`、書込みはSTM32CubeProgrammerを使う（Arm GNU Toolchain 14.2.Rel1のzipとpipの`ninja`でもbuildできる）。`tools/`のscriptはPython 3とpyserialを使う。STM32CubeProgrammerがない場合、`fwctl.py flash`はdfu-util（環境変数`DFU_UTIL`またはPATH）で`.bin`を書いて読み戻し照合する。どちらもDFU（0483:DF11）にdriverが要る。

```sh
export PATH="/c/msys64/mingw64/bin:$PATH"
cmake -S firmware/target -B firmware/build/target -G Ninja
cmake --build firmware/build/target

py -3 -X utf8 firmware/tools/fwctl.py flash   # 引数なしはbringup.hex。DFUが見えていればそのまま、firmwareが動いていればdfu commandで移してから書く
py -3 -X utf8 firmware/tools/fwctl.py check   # id/clk/regs/pps/lseを実行し、firmware/test_results/<UID>/へ記録
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
| [`ts_dpll`](./core/include/ts_dpll.h) | 生成PPSをGNSS PPSへ合わせるDPLL（周期ARRだけを変えるPI制御、補正は±100 us/秒以内） | UT-10 |
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

`TIME_STATUS`の`last error`は、`0x0000`=none、`0x0001`～`0x00FF`=直近に返したNACKのcode、`0x0101`=frame CRC、`0x0102`=frame length、`0x0103`=frame version、`0x0104`=inter-byte timeout。一度でもNACKを返すと、以後はframe errorよりNACKのcodeを優先して出し、resetまで消えない。
