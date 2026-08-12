# MCU・信号割当

- 対象MCU: `STM32G0B1RBT6`、LQFP-64
- 状態: 2026-08-11にDR-012として物理pin番号を全面修正した。**旧版のpin 1～32および40～41は実物と不一致であり使用してはならない。** GPIO名とAF番号は旧版のまま正しい
- 根拠: **ST `STM32G0B1xB/xC/xE` datasheet DS13560 Rev 6 のFigure 8（GP版pinout）およびTable 12（LQFP64-GP列）** で全pin確認済み。補助としてCubeMX pin database `mcu/STM32G0B1R(B-C-E)Tx.xml`、ST生成pinctrlデータ（AF番号）、CMSISヘッダ`stm32g0b1xx.h`（timer bit幅）、KiCad symbol library
- **品種の固定: 本表は`STM32G0B1RBT6`（GP版、`_RxT`）専用である。** N接尾辞品`STM32G0B1RBT6N`（`_RxTxN`）はpin 40/41が`VSS`/`VDDIO2`で、PD8/PD9が存在しない非互換pinoutである（DS13560 Figure 8、Table 2）。BOMのMPNをN接尾辞なしで固定し、差し替えない。本設計はUCPDを使用しないためGP版で不足はない
- 最新レビュー: [`../reviews/pre_easyeda_design_review.md`](../reviews/pre_easyeda_design_review.md)
- 旧レビュー: [`../reviews/final_design_review.md`](../reviews/final_design_review.md)（pin番号照合の結論はDR-012で覆された）

2026-08-10にLQFP-48の`STM32G0B1CBT6`から変更した。旧LQFP-48のpin番号は使用せず、以下を製造用割当の正本とする。

本パッケージの電源・基準pinはVBAT（6）、VREF+（7）、VDD（8）、VSS（9）の4本だけである。**`VDDIO2`、`VDDA`、`VSSA`、`VDDUSB`のpinは存在しない。** pin 40/41はPD8/PD9であり、電源へ接続してはならない。

LQFP-64再割当へ適用した確定事項:

- GNSS PPS/NMEA、Jetson/AUX入力5回路はBasicの`74HC14D,653` / `C5605` x1へ集約する。UART入力は各USARTの`RXINV`、PPS入力は反転edge captureを使う。各U4入力nodeには未接続時biasを設ける。
- 3.3 V PPS/UART出力の`74LVC125A`はU7/U8/U12の3個とし、J3～J8の全12出力を担当する。U7C/U7DをVLP-32CのPPS/NMEAへ使用する。
- VLP向け論理反転はUSARTの`TXINV`で行う。旧VLP専用driverと`+4V7_VLP`は使用しない。**reference U5/U6はDR-033でclock LDO/bufferへ再割当した。**
- VLP公式条件はHigh >3.0 Vかつ<5.0 V、Low <1.2 V、High時2 mA供給である。3.3 V `74LVC125A`にはこの組合せの直接min保証点がないため、J4を2 mA負荷で実測して試作合否を判定する。
- GH信号ESD footprintは設けない。通常出力は33 Ohm、VLPの2出力は10 Ohm、HC14入力は4.7 kOhmを初期値とする。
- USB ESDは実装し、VBUS PTC/TVSはfootprintを残して初回DNPとする。F1と並列のR36 `0603WAF0000T5E` / C21189を実装してVBUSを導通させる。
- DR-002対応として、`PPS_OE_N`はPB1、`SPR_TX_OE_N`はPB2へ割り当てる。PB1/PB2はDS13560 Table 12 note 4の条件付きreset pull-down対象外であり、reset中は外付け100 kOhm pull-upにより74LVC125AをHigh-Zへ固定する。
- DR-014対応として、生成PPSはPA6/TIM3_CH1ではなく**PC5/TIM2_CH2**から出力する。TIM3は16-bitで62.4 MHzから高分解能の1 Hzを生成できないため、32-bitのTIM2へ集約する。
- DR-018対応として、VREF+は`+3V3`へ直結し100 nFを付ける。`+3V3_A`は廃止する。ADCを使用しないためアナログrailを分離しない。VREF+を外部駆動するのでVREFBUFは有効化しない。

## VBUS電源経路

```text
J1 VBUS -> VBUS_RAW -> (F1 || R36) -> VBUS_PROTECTED -> U3 / board loads
                                      |
                                      +-> D1 TVS -> GND
```

- F1=`MF-NSMF050-2`、R36=`0603WAF0000T5E` / C21189とし、同じ`VBUS_RAW`―`VBUS_PROTECTED`間へ並列接続する。
- 初回variantはF1=DNP、R36=PCBA実装、D1=DNPとする。
- 保護variantはF1=実装、R36=DNPとする。F1とR36を同時実装するとPTCがbypassされるため禁止する。
- R36は0 Ohm、抵抗50 mOhm未満、0603 jumper定格1 Aで、基板VBUS設計上限0.5 Aを満たす。0.5 A時の最大dropは25 mVとする。

## MCU割当

| Pin | GPIO | 機能 | Net | 接続先 |
|---:|---|---|---|---|
| 1 | PC11 | GPIO | `NC_PC11` | 未使用、回路図上NC |
| 2 | PC12 | GPIO | `NC_PC12` | 未使用、回路図上NC |
| 3 | PC13 | GPIO | `NC_PC13` | 未使用、回路図上NC。RTC_OUT/TAMPへ割り付けない |
| 4 | PC14-OSC32_IN | LSE | `LSE_IN` | X1 SC-32S、C22 load capacitor |
| 5 | PC15-OSC32_OUT | LSE | `LSE_OUT` | X1 SC-32S、C23 load capacitor |
| 6 | VBAT | Power | `VBAT_RTC` | BT1 CR2032正極。100 nF、充電禁止 |
| 7 | VREF+ | Power | `+3V3` | 100 nF。VDDA >= 2 VのときVREF+は2 V～VDDAの範囲が要求されるため`+3V3`直結で適合。VREFBUFは有効化しない |
| 8 | VDD/VDDA | Power | `+3V3` | 本パッケージ唯一のVDD。VDDAは内部でVDDへ結線されるためVDDA専用decouplingは不要。100 nF + bulk |
| 9 | VSS/VSSA | Power | `GND` | 本パッケージ唯一のVSS |
| 10 | PF0-OSC_IN | HSE bypass | `CLK_26M` | U6出力をR45=33 Ohm経由で入力。TCXO/PLLの正常clock源 |
| 11 | PF1-OSC_OUT | GPIO | `NC_PF1` | HSE bypass時は未使用、回路図上NC |
| 12 | PF2-NRST | NRST | `NRST` | reset button/SWD。R42 10 kOhm pull-up、C18 100 nF |
| 13 | PC0 | GPIO | `NC_PC0` | 未使用、回路図上NC |
| 14 | PC1 | GPIO | `NC_PC1` | 未使用、回路図上NC |
| 15 | PC2 | GPIO | `NC_PC2` | 未使用、回路図上NC |
| 16 | PC3 | GPIO | `NC_PC3` | 未使用、回路図上NC |
| 17 | PA0 | USART4_TX (AF4) | `VLP_NMEA_TX_RAW` | TXINV有効、U7D -> J4 pin 3 |
| 18 | PA1 | GPIO | `SYNC_STATUS` | GNSS追従/HOLDOVER/再同期状態 |
| 19 | PA2 | USART2_TX (AF1) | `JETSON_TX_RAW` | U7B -> J3 pin 3 |
| 20 | PA3 | USART2_RX (AF1) | `JETSON_RX` | U4C HC14出力、RXINV有効 |
| 21 | PA4 | USART6_TX (AF3) | `SPR_NMEA_TX_RAW` | U8D -> J6 pin 3。**`TT` = 3.6 V耐圧のみ** |
| 22 | PA5 | TIM2_CH1_ETR (AF2) | `GNSS_PPS_MON` | U4A HC14出力、falling-edge capture。**`TT` = 3.6 V耐圧のみ** |
| 23 | PA6 | TIM3_CH1 (AF1) | `TP_PPS_DEBUG` | DR-014により生成PPSから解放。監視用test padとして残す |
| 24 | PA7 | GPIO | `STATUS_LED_N` | status LED |
| 25 | PC4 | GPIO | `NC_PC4` | 未使用、回路図上NC。VBAT monitorを採用する場合の候補（OI-005） |
| 26 | PC5 | TIM2_CH2 (AF2) | `PPS_GENERATED` | **常時使用するMCU hardware timer PPS。32-bit TIM2で生成** |
| 27 | PB0 | USART5_TX (AF8) | `MID_NMEA_TX_RAW` | U8B -> J5 pin 3 |
| 28 | PB1 | GPIO | `PPS_OE_N` | U7/U8/U12のPPS channel共通enable、100 kOhm pull-up |
| 29 | PB2 | GPIO | `SPR_TX_OE_N` | Spresense TX buffer enable、100 kOhm pull-up |
| 30 | PB10 | GPIO | `NC_PB10` | 未使用、回路図上NC。TIM2_CH3は解放 |
| 31 | PB11 | USART3_RX (AF4) | `GNSS_NMEA_RX` | U4B HC14出力、RXINV有効 |
| 32 | PB12 | GPIO | `JET_TX_OE_N` | Jetson TX buffer enable、100 kOhm pull-up |
| 33 | PB13 | GPIO | `VLP_TX_OE_N` | VLP TX buffer enable、100 kOhm pull-up |
| 34 | PB14 | GPIO | `MID_TX_OE_N` | Mid-360 TX buffer enable、100 kOhm pull-up |
| 35 | PB15 | GPIO | `NC_PB15` | 未使用、回路図上NC。条件付きreset pull-down対象のため安全制御には使用しない |
| 36 | PA8 | GPIO | `NC_PA8` | 未使用、回路図上NC。条件付きreset pull-down対象のため安全制御には使用しない |
| 37 | PA9 | USART1_TX (AF1) | `AUX1_TX_RAW` | U12B -> J7 pin 3 |
| 38 | PC6 | LPUART2_TX (AF3) | `AUX2_TX_RAW` | U12D -> J8 pin 3 |
| 39 | PC7 | LPUART2_RX (AF3) | `AUX2_RX` | U4E HC14出力、RXINV有効 |
| 40 | PD8 | GPIO | `NC_PD8` | 未使用、回路図上NC。**電源へ接続しない** |
| 41 | PD9 | GPIO | `NC_PD9` | 未使用、回路図上NC。**電源へ接続しない（旧版はVDDIO2と誤記）** |
| 42 | PA10 | USART1_RX (AF1) | `AUX1_RX` | U4D HC14出力、RXINV有効 |
| 43 | PA11 | USB_DM | `USB_D-` | USB Type-C |
| 44 | PA12 | USB_DP | `USB_D+` | USB Type-C |
| 45 | PA13 | SWDIO | `SWDIO` | SWD pad TP1 |
| 46 | PA14-BOOT0 | SWCLK/BOOT0 | `SWCLK_BOOT0` | SWD pad TP2/BOOT control |
| 47 | PA15 | GPIO | `TP_SPARE4` | 予備/test pad。専用DFU buttonには使用しない |
| 48 | PC8 | GPIO | `NC_PC8` | 未使用、回路図上NC |
| 49 | PC9 | GPIO | `NC_PC9` | 未使用、回路図上NC |
| 50 | PD0 | GPIO | `TP_SPARE6` | spare/test pad |
| 51 | PD1 | GPIO | `TP_SPARE7` | spare/test pad |
| 52 | PD2 | GPIO | `TP_SPARE8` | spare/test pad |
| 53 | PD3 | GPIO | `TP_SPARE9` | spare/test pad |
| 54 | PD4 | GPIO | `NC_PD4` | 未使用、回路図上NC |
| 55 | PD5 | GPIO | `NC_PD5` | 未使用、回路図上NC |
| 56 | PD6 | GPIO | `NC_PD6` | 未使用、回路図上NC |
| 57 | PB3 | GPIO | `PPS_PATH_STATUS` | PPS出力状態/test |
| 58 | PB4 | GPIO | `GNSS_NMEA_VALID` | firmware status/test |
| 59 | PB5 | GPIO | `TP_SPARE5` | spare/test pad |
| 60 | PB6 | GPIO | `NC_PB6` | 未使用、回路図上NC |
| 61 | PB7 | GPIO | `NC_PB7` | 未使用、回路図上NC |
| 62 | PB8 | GPIO | `AUX1_TX_OE_N` | 予備UART 1 TX buffer enable、100 kOhm pull-up |
| 63 | PB9 | GPIO | `AUX2_TX_OE_N` | 予備UART 2 TX buffer enable、100 kOhm pull-up |
| 64 | PC10 | GPIO | `NC_PC10` | 未使用、回路図上NC |

pin数の検算: I/O 60本（PA0～PA15、PB0～PB15、PC0～PC15、PD0～PD6、PD8、PD9、PF0～PF2）+ 電源4本 = 64。ST CubeMX pin databaseの`IONb=60`と一致する。

`VDD`と`VREF+`は3.3 V系、`VBAT`はCR2032由来の`VBAT_RTC`へ接続し、各電源pin直近へ100 nFを置く。

PA9/PA11とPA10/PA12のremapについて、ST CubeMX pin databaseの記述は次のとおりである。**default状態を使用し、remapは有効化しない。**

| Position | default | remap variant |
|---:|---|---|
| 37 | `PA9` | `UCPD1_DBCC1`（PINREMAP時） |
| 42 | `PA10` | `UCPD1_DBCC2`（PINREMAP_10_12時） |
| 43 | `PA11 [PA9]` | `PA9 [PA11]`（PINREMAP時） |
| 44 | `PA12 [PA10]` | `PA10 [PA12]`（PINREMAP_10_12時） |

本パッケージはPA9（pin 37）とPA11（pin 43）、PA10（pin 42）とPA12（pin 44）を別pinとして持つため、`AUX1_TX_RAW`（USART1_TX / PA9）と`USB_D-`（PA11）、`AUX1_RX`（USART1_RX / PA10）と`USB_D+`（PA12）は競合しない。remapを有効化するとpin 37/42がUCPD用になるため、SYSCFGのremap bitはreset値のままにする（DS13560 Table 12 note 5）。

### 耐圧とdomainの注意

- **PA4とPA5は`TT`（3.6 V tolerant）であり、他の使用pinの`FT`（5 V tolerant）とは異なる。** PA5はU4A（3.3 V）出力駆動、PA4は出力専用のため現構成は適合するが、**この2 pinへ5 V信号を接続してはならない。** VLP-32C系（High上限5.0 V）をMCUへ直結する変更を行う場合は`FT` pinを選ぶ。
- **PC13～PC15の制約（DS13560 Table 12 note 1/2）:** RTC power switch経由のdomainである。PC14/PC15はLSE専用、PC13は未使用とし、firmwareはTAMP/RTC_OUTをPC13へ割り付けない。

### VDDIO2 domainのI/O

DS13560 Table 11の`_s`は「I/O, supplied from VDDIO2 only」を意味する。本パッケージにVDDIO2 pinはなく内部でVDDへ結線されるが、`PWR_CR2`の`PVM_VDDIO2[2:0]`はreset値000で「Monitoring disabled; IOs in isolation mode」である。**firmware初期化で`PVM_VDDIO2 = 110`（Monitoring bypassed; IOs enabled）を設定する。**

本設計でVDDIO2 domainに属するnetは次のとおりである。

| Net | Pin | I/O structure |
|---|---|---|
| `AUX1_TX_RAW` / `AUX1_RX` | PA9 / PA10 | `FT_s` / `FT_fds` |
| `USB_D-` / `USB_D+` | PA11 / PA12 | `FT_fus` |
| `SWDIO` / `SWCLK_BOOT0` | PA13 / PA14 | `FT_es` / `FT_s` |
| `TP_SPARE4` | PA15 | `FT_s` |
| `VLP_TX_OE_N` / `MID_TX_OE_N` | PB13 / PB14 | `FT_fs` |
| `AUX2_TX_RAW` / `AUX2_RX` | PC6 / PC7 | `FT_s` |
| `TP_SPARE6`～`TP_SPARE9` | PD0～PD3 | `FT_cs` / `FT_ds` |

- **powered時刻機能の中核はVDD domainである。** `CLK_26M`（PF0）、`PPS_GENERATED`（PC5）、`GNSS_PPS_MON`（PA5）、Jetson UART（PA2/PA3）、VLP/Mid/Spresense TX（PA0/PB0/PA4）、`PPS_OE_N`（PB1）はいずれも`_s`ではない。LSE/内蔵RTC/VBATはbackup domainで暦を保持する。
- `VLP_TX_OE_N`/`MID_TX_OE_N`が万一isolationされても、外付け100 kOhm pull-upがbufferをHigh-Zへ保つため安全側に失敗する。
- **診断:** 「Jetson UART・PPS・RTCは動作するが、USBが列挙されず、J7/J8が無反応で、VLPとMid-360のNMEAだけ出ない」場合は`PVM_VDDIO2`を最初に確認する。

### EasyEDA入力時の必須確認

EasyEDA/LCSCが提供するMCU symbolを配置した直後に、**symbol上のpin番号と本表を1本ずつ突き合わせる。** 旧版の表はpin 1～32および40～41が実物と不一致だったため（DR-012）、表を信用してsymbol確認を省略してはならない。特に次の4点を確認する。

1. pin 8がVDD、pin 9がVSSであること。pin 12がPF2-NRSTであること。
2. pin 40/41がPD8/PD9であり、電源netへ接続されていないこと。
3. pin 3がPC13（`NC_PC13`）、pin 4/5がPC14/PC15（LSE）、pin 10がPF0（`CLK_26M`）、pin 26がPC5（`PPS_GENERATED`）であること。
4. `VDDIO2`という名前のpinが存在しないこと。

## MCU clock構成

| 項目 | 構成 | 備考 |
|---|---|---|
| SYSCLK / timer clock源 | 26 MHz TCXO + HSE bypass + PLL、62.4 MHz | PLLM=5、PLLN=24、PLLR=2。PF0入力はduty 45～55 %、PF1 NC。Voltage Range 1、Flash 2 wait state |
| timer clockの周波数基準 | **Q2C26000V2D4B2、±0.5 ppm** | 2.8 V TCXOから3.3 V Schmitt buffer経由でTIM2を直接駆動 |
| UTCの絶対基準 | GNSS PPS + 検証済みNMEA | GNSS有効中はDPLLで位相・周波数を規律する |
| USB clock | HSI48 + CRS | **CRSはUSB clockのみを規律し、timer clockには影響しない。水晶の要否はUSBとは独立した問題である。** CRSの同期源であるSOFは列挙後にしか来ないため、初回列挙はHSI48の工場trim精度で通す必要がある |
| UART baud生成 | TCXO/PLL由来 | ±0.5 ppm級。追加BRR補正は不要 |
| calendar / power-off保持 | STM32内蔵RTC + X1 LSE + CR2032 | LSEは±20 ppm級。powered holdoverのPPS基準には使わない |

## PPS/holdoverのtimer構成

生成PPSと入力captureは**すべて32-bit TIM2へ集約する。** `stm32g0b1xx.h`の`IS_TIM_32B_COUNTER_INSTANCE(INSTANCE) ((INSTANCE) == TIM2)`により、32-bit counterを持つtimerはTIM2だけである。16-bitのTIM3ではARR上限65535のためtick 15.26 us以上となり、PPS位相1 us目標と周波数分解能3.33 ppmを満たせない。

| TIM2 channel | Pin | 用途 |
|---|---|---|
| CH1 (AF2) | PA5 / pin 22 | GNSS PPS input capture（HC14反転のためfalling edge） |
| CH2 (AF2) | PC5 / pin 26 | 生成PPS output compare / PWM |
| CH3 | 使用しない | PB10は未使用GPIO |
| CH4 | 使用しない | PA3/PB11はUSART2_RX/USART3_RXが使用する |

- `ARR = 62399999`（ARPE有効）、`CCR2 = 6240000`で周期1秒・High幅100 msとなる。tickは約16.026 ns、周期補正1 LSBは約0.0160 ppmである。
- ARRを1秒に設定するとcounterが「秒内位相」そのものになり、CH1のcapture値がそのまま位相誤差になる。
- **PWM mode 1・上カウント・active highでは、出力は`CNT < CCR2`の間Highとなる。したがってPPSの立上りedgeはcounter reset（update event）で決まり、`CCR2`が動かすのは立下りedge＝High幅だけである。**
  - 周波数補正も位相補正も**ARR**で行う。位相はQ-017の±100 us/s以内でARRを増減し、秒境界を歩かせて戻す。
  - `CCR2 = 6240000`はHigh幅100 ms（Q-011）を決める固定値であり、ARRを変えても更新しない。
  - `CCR2`を位相補正に使わない。使ってもPPSの基準edgeは動かない。
- TIM2は他用途へ割り当てない。PA6/TIM3_CH1は監視用test padとして残す。
- `PPS_GENERATED`をPC5へ移したことによるreset時の懸念はない。reset中の出力状態は`PPS_OE_N`（PB1、100 kOhm pull-up）が74LVC125AをHigh-Zに保つことで決まり、PC5のreset時状態には依存しない。

## コネクタ割当

| Ref | Part | Pin 1 | Pin 2 | Pin 3 | Pin 4 |
|---|---|---|---|---|---|
| J2 | GH 3 pin / UM982 | GND | `UM_PPS_IN` | `UM_NMEA_IN` | - |
| J3 | GH 4 pin / Jetson | GND | `JET_PPS_OUT` | `BOARD_TX_JET_RX` | `BOARD_RX_JET_TX` |
| J4 | GH 3 pin / VLP-32C | GND | `VLP_PPS_OUT` | `VLP_NMEA_INV_OUT` | - |
| J5 | GH 3 pin / Mid-360 | GND | `MID_PPS_OUT` | `MID_NMEA_OUT` | - |
| J6 | GH 3 pin / Spresense level board | GND | `SPR_PPS_3V3_OUT` | `SPR_NMEA_3V3_OUT` | - |
| J7 | GH 4 pin / AUX UART 1 | GND | `AUX1_PPS_OUT` | `BOARD_TX_AUX1_RX` | `BOARD_RX_AUX1_TX` |
| J8 | GH 4 pin / AUX UART 2 | GND | `AUX2_PPS_OUT` | `BOARD_TX_AUX2_RX` | `BOARD_RX_AUX2_TX` |

この表は基板側J2～J8の割当である。相手機器側connector、pin番号、線色は試作ハーネス製作前のTBDとし、[`harness_pinout.md`](./harness_pinout.md)で管理する。基板側割当を変更する場合は本表、BOM、ハーネス表を同時に更新する。

## 出力buffer channel割当

3.3 V出力はU7/U8/U12 `74LVC125A`を使用する。U9/U13は使用しない。U5は2.8 V clock LDO、U6は74LVC1G17 clock bufferへ再割当する。

| IC channel | Input | Output | `/OE` net | Connector |
|---|---|---|---|---|
| U7A | `PPS_GENERATED` | `JET_PPS_OUT` | `PPS_OE_N` | J3 pin 2 |
| U7B | `JETSON_TX_RAW` | `BOARD_TX_JET_RX` | `JET_TX_OE_N` | J3 pin 3 |
| U7C | `PPS_GENERATED` | `VLP_PPS_OUT` | `PPS_OE_N` | J4 pin 2、3.3 V試作出力 |
| U7D | `VLP_NMEA_TX_RAW` (`TXINV=1`) | `VLP_NMEA_INV_OUT` | `VLP_TX_OE_N` | J4 pin 3、3.3 V試作出力 |
| U8A | `PPS_GENERATED` | `MID_PPS_OUT` | `PPS_OE_N` | J5 pin 2 |
| U8B | `MID_NMEA_TX_RAW` | `MID_NMEA_OUT` | `MID_TX_OE_N` | J5 pin 3 |
| U8C | `PPS_GENERATED` | `SPR_PPS_3V3_OUT` | `PPS_OE_N` | J6 pin 2 |
| U8D | `SPR_NMEA_TX_RAW` | `SPR_NMEA_3V3_OUT` | `SPR_TX_OE_N` | J6 pin 3 |
| U12A | `PPS_GENERATED` | `AUX1_PPS_OUT` | `PPS_OE_N` | J7 pin 2 |
| U12B | `AUX1_TX_RAW` | `BOARD_TX_AUX1_RX` | `AUX1_TX_OE_N` | J7 pin 3 |
| U12C | `PPS_GENERATED` | `AUX2_PPS_OUT` | `PPS_OE_N` | J8 pin 2 |
| U12D | `AUX2_TX_RAW` | `BOARD_TX_AUX2_RX` | `AUX2_TX_OE_N` | J8 pin 3 |
- PPS 6 channelは入力だけを`PPS_GENERATED`へ共通接続し、出力は相互接続しない。
- 各ICのVCC-GND間へ100 nF decouplingを1個ずつ配置する。
- すべての`/OE`はactive-low。reset中はpull-upでHigh-Zとする。

### VLP 3.3 V試作出力

- VLP-32C公式条件はPPS/NMEAともHigh >3.0 Vかつ<5.0 V、Low <1.2 V、High時2 mA供給である。
- U7C/U7Dは`+3V3`駆動のためHigh上限5.0 V未満とLow条件は満たす。Nexperia 74LVC125AはVCC=3.0 V、IOH=-18 mAでVOH min 2.4 Vを規定するが、3.3 V・2 mAに対するVOH minは表で直接規定していない。このためHigh >3.0 V @ 2 mAは文書上のworst-case保証ではなく試作実測項目とする。
- J4 pin 2/3を各1.5 kOhmでGNDへ負荷し、R15/R16=10 Ohm後のHighが3.0 Vを超え、Lowが1.2 V未満であることを全試作基板で確認する。規定されていない動作点の補間値は保証根拠に使わず、VLP実機でもPPS/NMEA lockを確認する。
- 不合格の場合は3.3 V出力を無理に使用せず、次版で専用level driverを再設計する。

## PPSロジック接続

```text
J2 UM_PPS_IN -> U4A 74HC14 -> GNSS_PPS_INV -> MCU PA5 TIM2_CH1 falling-edge capture
MCU PC5 TIM2_CH2 PPS_GENERATED -> U7A/U7C/U8A/U8C/U12A/U12C -> J3/J4/J5/J6/J7/J8 PPS
MCU PB1 PPS_OE_N -> all six PPS channels /OE, 100 kOhm pull-up
```

GNSS正常時もPPSを直接pass-throughせず、PC5のTIM2出力比較をGNSS PPSへ規律する。GNSS復帰時は周期を1秒当たり最大±100 usだけ変化させて位相を連続的に戻す。

GNSS PPS経路の固定遅延は、R27（4.7 kOhm）とU4A入力容量および配線容量によるRC、U4Aの伝搬遅延、capture同期遅延の和である。RC分は概算55～82 nsで、U4のしきい値ばらつきによる基板間差は約27 nsとなる。これらは基板ごとの固定offsetとして実測・校正し、DPLLの位相補正値へ含める。0～40 °Cでのドリフトは10 ns級であり、1 us目標に対して支配的ではない。

### U4外部入力bias

R27～R31の4.7 kOhm直列抵抗よりU4側に、次の100 kOhm biasを配置する。これによりJ2/J3/J7/J8未接続時もSchmitt入力が浮かない。

| Reference | U4 channel / signal | Bias | Default状態 |
|---|---|---|---|
| R37 | U4A / `UM_PPS_IN` | 100 kOhm to GND | PPS inactive Low |
| R38 | U4B / `UM_NMEA_IN` | 100 kOhm to `+3V3` | UART idle High |
| R39 | U4C / `BOARD_RX_JET_TX` | 100 kOhm to `+3V3` | UART idle High |
| R40 | U4D / `BOARD_RX_AUX1_TX` | 100 kOhm to `+3V3` | UART idle High |
| R41 | U4E / `BOARD_RX_AUX2_TX` | 100 kOhm to `+3V3` | UART idle High |

U4F inputは従来どおりGNDへ固定し、outputはNCとする。

## NMEAロジック接続

```text
J2 UM_NMEA_IN -> U4B 74HC14 -> MCU USART3_RX (RXINV=1)
MCU PA2 USART2_TX -> U7B -> J3 BOARD_TX_JET_RX
J3 BOARD_RX_JET_TX -> U4C 74HC14 -> MCU USART2_RX (RXINV=1)
MCU USART4_TX (TXINV=1) -> U7D 74LVC125A -> J4 VLP_NMEA_INV_OUT
MCU PB13 VLP_TX_OE_N -> U7D /OE, 100 kOhm pull-up
MCU PB0 USART5_TX -> U8B -> J5 MID_NMEA_OUT
MCU PA4 USART6_TX -> U8D -> J6 SPR_NMEA_3V3_OUT
MCU PB2 SPR_TX_OE_N -> U8D /OE, 100 kOhm pull-up
MCU PA9 USART1_TX -> U12B -> J7 BOARD_TX_AUX1_RX
J7 BOARD_RX_AUX1_TX -> U4D 74HC14 -> MCU USART1_RX (RXINV=1)
MCU PC6 LPUART2_TX -> U12D -> J8 BOARD_TX_AUX2_RX
J8 BOARD_RX_AUX2_TX -> U4E 74HC14 -> MCU LPUART2_RX (RXINV=1)
U4F 74HC14 input -> GND; output -> NC
```

6系統のTXはすべて別のUART/LPUART peripheralと別のbuffer channelを使う。各bufferの`OE_N`も個別GPIOで制御し、reset中は100 kOhm pull-upでHigh-Zとする。外部出力の直列抵抗は通常33 Ohm、VLP PPS/NMEAのR15/R16だけ10 Ohmとする。HC14外部入力は4.7 kOhm初期値の直列抵抗を経由させる。VLPのUSART TX pinはreset中の外部buffer出力がHigh-Zになるよう`OE_N`で制御する。

## 試作機の電源順序制約

U4 `74HC14D,653`はIoff非対応であり、4.7 kOhmは入力clamp電流を制限するがback-powerを遮断しない。外部3.3 V Highを基板OFF中に印加した場合、1入力当たり概算0.6 mA以下、5入力合計で概算3 mA以下に制限されるが、3.3 V railやMCUの部分給電状態は機能保証されない。初回試作は追加isolationを実装せず、次を必須運用条件とする。

1. J2～J8は全機器の電源OFF中に接続・切断し、hot-plugしない。
2. 入力を駆動するUM982、Jetson、AUXより先、または同時に本基板へ給電する。本基板だけを先にOFFしない。
3. 接続先をOFFする前に`PPS_OE_N`と各`*_TX_OE_N`をHighへして全出力をHigh-Zにする。
4. firmware初期化完了までは全`OE_N`をHighに保ち、接続先の電源ON確認後に必要channelだけenableする。

この条件を守れない用途、hot-plug、量産版では、U4のIoff対応品への変更および接続先power sense/isolationを再検討する。

## TCXO、HSE bypass、LSE接続

```text
+3V3 -> U5 XC6206P282MR -> +2V8_CLK
+2V8_CLK -> U2 Q2C26000V2D4B2 VDD
U2 CMOS OUT -> U6 74LVC1G17GW input
+3V3 -> U6 VCC
U6 OUT -> R45 33 Ohm -> CLK_26M -> MCU PF0/OSC_IN
MCU PF1/OSC_OUT -> NC
U2 ENABLE/ST -> R46 10 kOhm -> +2V8_CLK

MCU PC14/OSC32_IN -- X1 SC-32S 32.768 kHz -- MCU PC15/OSC32_OUT
PC14 -- C22 10 pF C0G ±5% -- GND
PC15 -- C23 10 pF C0G ±5% -- GND
BT1 CR2032+ -> VBAT_RTC -> MCU VBAT
BT1 CR2032- -> GND
```

- U5入出力へ各1 uF、U2/U6の電源pin直近へ各100 nFを置く。`+2V8_CLK`はU2専用とする。
- X1のCL=7 pFに対し基板寄生を約2 pFと仮定し、C22/C23は10 pFを初期値とする。SC-32SのESR最大70 kOhm、C0 typ 1 pF、CL 7 pFから`gmcrit`は概算0.76 uA/Vとなるため、STM32は`LSEDRV=10`（medium-high、`Gmcritmax` 1.7 uA/V）で使用する。試作で8.2～12 pFの範囲を調整し、起動余裕を確認する。
- U2→U6→PF0はGND plane上を短く配線し、R45はPF0直近へ置く。clock配線のtest padは小型・stub最小とする。
- X1とC22/C23はPC14/PC15直近へ配置し、X1直下および発振loop内には信号配線やcopper pourを通さない。PC13はNCのままとする。
- U2の個別pin配置、enable/standby極性、起動時間、±0.5 ppmの温度範囲はメーカー個別datasheetとEasyEDA libraryを発注前に照合する。
- HSE Clock Security Systemを有効化する。HSE/PLL faultでHSIへ切り替わった場合、時刻出力は継続せず全`OE_N`をHighにして`FAULT_CLOCK`へ遷移する。

## RESET / BOOT / DFU

- SW1 `RESET`は`NRST`（pin 12）とGND間。R42 10 kOhmで`NRST`を`+3V3`へpull-upし、C18 100 nFを`NRST`-GND間へ配置する。
- SW2 `BOOT`は`+3V3`からR44 1 kOhmを介してPA14-BOOT0へ接続する。R43 100 kOhmでPA14-BOOT0をGNDへpull-downする。
- SW1/SW2は小型Basic `TS-1088-AR02016` / C720477、SMD 4 x 3 mmを使用する。
- PA14はSWCLK兼用である。STM32G0B1 errata ES0548 §2.2.8により、debug接続後はprobeを外しただけではBOOT0 sampling状態が戻らない。SWD probeを外し、USBを含む全主電源を切って完全power cycleした後にSW2 + SW1のBOOT操作を行う。
- option byteを`nBOOT_SEL=0`、`nBOOT1=1`として外部BOOT0とsystem-memory bootを有効化する（AN2606の一般表では`nBOOT_SEL`を`nBOOT0_SEL`と表記）。通常起動はBOOT Low、system-memory USB DFUはSW2を押したままSW1を押して離し、その後SW2を離す。
- PA15の専用`DFU_REQ_N` buttonは設けない。正常firmwareからはUSB CDC commandによるsystem bootloader移行を実装し、firmware異常時はSW1/SW2を使用する。
- SWD recovery用TP1～TP5（SWDIO、SWCLK、NRST、3V3、GND）は維持する。

### 初回bring-up手順（DR-021）

**JLCPCBのPCBAはfirmware書込みもoption byte設定も行わない。** 上記のoption byte設定は基板受領後にユーザーが実施する工程である。レジスタの所在も区別する。

- `nBOOT_SEL`（bit 24）、`nBOOT1`（bit 25）、`nBOOT0`（bit 26）は`FLASH_OPTR`のoption byteである。
- `BOOT_LOCK`はoption byteではなく`FLASH_SECR`のbit 16である。**初回試作では変更せず既定値のままとする。**

未書込みの新品基板は空flash判定（RM0444 §2.5.4、`FLASH_ACR`の`EMPTY` bit）でsystem bootloaderへ入るため、SWD probeなしでもUSB DFUで認識できる。判定はmainフラッシュがboot対象かつ`BOOT_LOCK`未セットのときに行われる。次の順序で立ち上げる。

1. 新品基板をUSBへ接続し、空flash判定によりsystem bootloaderが起動してDFU deviceとして見えることを確認する。DFUで初回firmwareを書き込む。
2. **書込み後はUSBを抜き差しして電源を入れ直す。** `EMPTY`フラグのクリアにはpower on resetまたは`OBL_LAUNCH`が必要で、**system reset（SW1）では clear されない。** SW1だけを押しても新firmwareは起動せず再びbootloaderへ入る。
3. option byteを`nBOOT_SEL=0`、`nBOOT1=1`へ設定し、SW2 + SW1によるDFU移行を有効にする。BOOT0はNRSTの立上りedgeでサンプリングされるため、SW2を押したままSW1を押して離す手順が正しい。
4. 上記が失敗した場合はTP1～TP5経由のSWDで書き込む。SWD使用後にBOOT+RESETを再試行するときは、probeを外して全主電源を完全power cycleしてから行う。

## firmware初期化の必須項目

回路ではなくfirmwareでしか担保できない条件を、bring-up前の確認項目として記録する。

| 項目 | 内容 |
|---|---|
| `OE_N`初期値 | 全`OE_N` GPIOは出力値をHighへ設定した後にoutput modeへ切り替える。Low glitchを出さない |
| VREFBUF | VREF+を外部から3.3 Vで駆動するため有効化しない |
| `PWR_CR2`の`PVM_VDDIO2[2:0]` | **`110`（Monitoring bypassed; IOs enabled）を設定する。** reset値000は「IOs in isolation mode」であり、USB・SWD・AUX1/AUX2 UART・VLP/Mid TXのOE_NがVDDIO2 domainに属する。上記「VDDIO2 domainのI/O」参照 |
| `SYSCFG_CFGR1`の`UCPD1_STROBE` / `UCPD2_STROBE` | **起動時にstrobeしてdead-battery pull-downを無効化する。** DS13560 Table 12 note 4により、reset時PA8/PB15/PD0/PD2にpull-downが present し得る（PA9/PA10/PD1/PD3の電圧に依存）。PA8/PB15は未使用、PD0～PD3はtest padのため現状は無害だが、test padを使う場合に必要 |
| clock tree | HSE bypass、HSE=26 MHz、PLLM=5、PLLN=24、PLLR=2、SYSCLK/TIM2=62.4 MHzを設定。Voltage Range 1の`VOSF=0`確認後、Flashを2 wait stateへ設定してからSYSCLKを切り替える。PF0 duty 45～55 %を前提とし、HSE CSSを有効化する |
| 内蔵RTC | LSEをclock源とし、`LSEDRV=10`、`LSECSSON=0`とする。backup registerにsignature/version、TCXO補正値、同期時刻、application payload CRC、RTC-domain configuration CRCを保存する。PC13へTAMP/RTC_OUTを割り付けない |
| timer edge極性 | GNSS PPSはU4Aで反転するためTIM2_CH1 falling-edge capture |
| clock規律 | GNSS有効中はCH1 PPSからTCXO実偏差と位相を推定する。holdoverでは最後に検証済みのARR補正値を保持する |
| UART baud | TCXO/PLL由来。通常はBRRの動的補正不要 |
| TIM2構成 | `ARR = 62399999`、ARPE有効、`CCR2 = 6240000`（High幅100 ms固定）。**周波数補正と位相補正はいずれもARRで行う。** |
| RTC有効性 | 起動処理の最初かつreset flag clear前に`BORRSTF`を保存し、signature/version、両CRC、`INITS`、RTCSEL/LSEON/LSERDY/RTCEN、calendar範囲、SSR単調性を確認する。CRC/設定異常ではBDRSTをset/read-back/clearし、外部へ時刻を出さない。正常確認後にreset flagをclearする。`RTC_VALID`は主電源断中のLSE連続動作や電池残量を保証しない |
| clock fault | HSE/PLL異常時はHSIへ診断退避し、全`OE_N`をHigh、`FAULT_CLOCK`へ遷移する |
