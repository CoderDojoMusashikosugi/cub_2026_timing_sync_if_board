# MCU・信号割当

- 対象MCU: `STM32G0B1RBT6`、LQFP-64
- 状態: 2026-08-11にLQFP-64の物理pin 1～64と使用AFを再照合し、pin番号/AF番号の不一致およびGPIO/netの重複なし。PA8/PB15のreset時pull-down条件はPB1/PB2への再割当で解消済み
- 根拠: STMicroelectronics `STM32G0B1xB/xC/xE` datasheet DS13560 Rev 6、Figure 8およびTable 12～18
- 最終レビュー: [`../reviews/final_design_review.md`](../reviews/final_design_review.md)

2026-08-10にLQFP-48の`STM32G0B1CBT6`から変更した。旧LQFP-48のpin番号は使用せず、以下を製造用割当の正本とする。

LQFP-64再割当へ適用した確定事項:

- GNSS PPS/NMEA、Jetson/AUX入力5回路はBasicの`74HC14D,653` / `C5605` x1へ集約する。UART入力は各USARTの`RXINV`、PPS入力は反転edge captureを使う。各U4入力nodeには未接続時biasを設ける。
- 3.3 V PPS/UART出力の`74LVC125A`はU7/U8/U12の3個とし、J3～J8の全12出力を担当する。U7C/U7DをVLP-32CのPPS/NMEAへ使用する。
- VLP向け論理反転はUSARTの`TXINV`で行う。試作簡略化により専用U5/U6/U9と`+4V7_VLP`は使用しない。
- VLP公式条件はHigh >3.0 Vかつ<5.0 V、Low <1.2 V、High時2 mA供給である。3.3 V `74LVC125A`にはこの組合せの直接min保証点がないため、J4を2 mA負荷で実測して試作合否を判定する。
- GH信号ESD footprintは設けない。出力は33 Ohm、HC14入力は4.7 kOhmを初期値とする。
- USB ESDは実装し、VBUS PTC/TVSはfootprintを残して初回DNPとする。F1と並列のR36 `0603WAF0000T5E` / C21189を実装してVBUSを導通させる。
- DR-002対応として、`PPS_OE_N`はPB1、`SPR_TX_OE_N`はPB2へ割り当てる。PB1/PB2はDS13560 Table 12 note 4の条件付きreset pull-down対象外であり、reset中は外付け100 kOhm pull-upにより74LVC125AをHigh-Zへ固定する。

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
| 1 | PC13 | GPIO | `RTC_INT_N` | DS3231 INT/SQW |
| 2 | PC14 | GPIO | `NC_PC14` | 未使用、回路図上NC |
| 3 | PC15 | GPIO | `NC_PC15` | 未使用、回路図上NC |
| 4 | VBAT | Power | `+3V3` | MCU backup domain。RTC電池とは分離 |
| 5 | VREF+ | Power | `+3V3_A` | analog reference、decoupling |
| 6 | VDD/VDDA | Power | `+3V3` | 100 nF + bulk decoupling |
| 7 | VSS/VSSA | Power | `GND` | ground |
| 8 | PF2-NRST | NRST | `NRST` | reset button/SWD |
| 9 | PF0-OSC_IN | GPIO | `TP_SPARE0` | spare/test pad |
| 10 | PF1-OSC_OUT | GPIO | `TP_SPARE1` | spare/test pad |
| 11 | PC0 | GPIO | `NC_PC0` | 未使用、回路図上NC |
| 12 | PC1 | GPIO | `NC_PC1` | 未使用、回路図上NC |
| 13 | PC2 | GPIO | `NC_PC2` | 未使用、回路図上NC |
| 14 | PC3 | GPIO | `NC_PC3` | 未使用、回路図上NC |
| 15 | PA0 | USART4_TX (AF4) | `VLP_NMEA_TX_RAW` | TXINV有効、U7D -> J4 pin 3 |
| 16 | PA1 | GPIO | `SYNC_STATUS` | GNSS追従/HOLDOVER/再同期状態 |
| 17 | PA2 | USART2_TX (AF1) | `JETSON_TX_RAW` | U7B -> J3 pin 3 |
| 18 | PA3 | USART2_RX (AF1) | `JETSON_RX` | U4C HC14出力、RXINV有効 |
| 19 | PA4 | USART6_TX (AF3) | `SPR_NMEA_TX_RAW` | U8D -> J6 pin 3 |
| 20 | PA5 | TIM2_CH1_ETR (AF2) | `GNSS_PPS_MON` | U4A HC14出力、falling-edge capture |
| 21 | PA6 | TIM3_CH1 (AF1) | `PPS_GENERATED` | 常時使用するMCU hardware timer PPS |
| 22 | PA7 | GPIO | `STATUS_LED_N` | status LED |
| 23 | PC4 | GPIO | `NC_PC4` | 未使用、回路図上NC |
| 24 | PC5 | GPIO | `NC_PC5` | 未使用、回路図上NC |
| 25 | PB0 | USART5_TX (AF8) | `MID_NMEA_TX_RAW` | U8B -> J5 pin 3 |
| 26 | PB1 | GPIO | `PPS_OE_N` | U7/U8/U12のPPS channel共通enable、100 kOhm pull-up |
| 27 | PB2 | GPIO | `SPR_TX_OE_N` | Spresense TX buffer enable、100 kOhm pull-up |
| 28 | PB10 | TIM2_CH3 (AF2) | `RTC_32K` | DS3231 32 kHz output |
| 29 | PB11 | USART3_RX (AF4) | `GNSS_NMEA_RX` | U4B HC14出力、RXINV有効 |
| 30 | PB12 | GPIO | `JET_TX_OE_N` | Jetson TX buffer enable、100 kOhm pull-up |
| 31 | PC11 | GPIO | `NC_PC11` | 未使用、回路図上NC |
| 32 | PC12 | GPIO | `NC_PC12` | 未使用、回路図上NC |
| 33 | PB13 | GPIO | `VLP_TX_OE_N` | VLP TX buffer enable、100 kOhm pull-up |
| 34 | PB14 | GPIO | `MID_TX_OE_N` | Mid-360 TX buffer enable、100 kOhm pull-up |
| 35 | PB15 | GPIO | `NC_PB15` | 未使用、回路図上NC。条件付きreset pull-down対象のため安全制御には使用しない |
| 36 | PA8 | GPIO | `NC_PA8` | 未使用、回路図上NC。条件付きreset pull-down対象のため安全制御には使用しない |
| 37 | PA9 | USART1_TX (AF1) | `AUX1_TX_RAW` | U12B -> J7 pin 3 |
| 38 | PC6 | LPUART2_TX (AF3) | `AUX2_TX_RAW` | U12D -> J8 pin 3 |
| 39 | PC7 | LPUART2_RX (AF3) | `AUX2_RX` | U4E HC14出力、RXINV有効 |
| 40 | VSS | Power | `GND` | digital ground |
| 41 | VDDIO2 | Power | `+3V3` | I/O supply、100 nF decoupling |
| 42 | PA10 | USART1_RX (AF1) | `AUX1_RX` | U4D HC14出力、RXINV有効 |
| 43 | PA11 | USB_DM | `USB_D-` | USB Type-C |
| 44 | PA12 | USB_DP | `USB_D+` | USB Type-C |
| 45 | PA13 | SWDIO | `SWDIO` | SWD pad |
| 46 | PA14-BOOT0 | SWCLK/BOOT0 | `SWCLK_BOOT0` | SWD pad/BOOT control |
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
| 60 | PB6 | I2C1_SCL (AF6) | `RTC_SCL` | DS3231 SCL、4.7 kOhm pull-up |
| 61 | PB7 | I2C1_SDA (AF6) | `RTC_SDA` | DS3231 SDA、4.7 kOhm pull-up |
| 62 | PB8 | GPIO | `AUX1_TX_OE_N` | 予備UART 1 TX buffer enable、100 kOhm pull-up |
| 63 | PB9 | GPIO | `AUX2_TX_OE_N` | 予備UART 2 TX buffer enable、100 kOhm pull-up |
| 64 | PC10 | GPIO | `NC_PC10` | 未使用、回路図上NC |

`VDD/VDDA`、`VDDIO2`、`VREF+`はすべて3.3 V系へ接続する。各電源pinのdecouplingと、PA11/PA12のUSB default mappingを回路図ERC時に確認する。角括弧で示されるPA11/PA12のPA9/PA10 remapは使用しない。

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

3.3 V出力はU7/U8/U12 `74LVC125A`を使用する。U5/U6/U9/U13は使用しない。

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
- J4 pin 2/3を各1.5 kOhmでGNDへ負荷し、33 Ohm直列抵抗後のHighが3.0 Vを超え、Lowが1.2 V未満であることを全試作基板で確認する。VLP実機でもPPS/NMEA lockを確認する。
- 不合格の場合は3.3 V出力を無理に使用せず、次版で専用level driverを再設計する。

## PPSロジック接続

```text
J2 UM_PPS_IN -> U4A 74HC14 -> GNSS_PPS_INV -> MCU timer falling-edge capture
MCU PA6 PPS_GENERATED -> U7A/U7C/U8A/U8C/U12A/U12C -> J3/J4/J5/J6/J7/J8 PPS
MCU PB1 PPS_OE_N -> all six PPS channels /OE, 100 kOhm pull-up
```

GNSS正常時もPPSを直接pass-throughせず、PA6のtimer出力をGNSS PPSへ規律する。GNSS復帰時は周期を1秒当たり最大±100 usだけ変化させて位相を連続的に戻す。

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

6系統のTXはすべて別のUART/LPUART peripheralと別のbuffer channelを使う。各bufferの`OE_N`も個別GPIOで制御し、reset中は100 kOhm pull-upでHigh-Zとする。すべての外部出力に33 Ohm初期値の直列抵抗を置く。HC14外部入力は4.7 kOhm初期値の直列抵抗を経由させる。VLPのUSART TX pinはreset中の外部buffer出力がHigh-Zになるよう`OE_N`で制御する。

## 試作機の電源順序制約

U4 `74HC14D,653`はIoff非対応であり、4.7 kOhmは入力clamp電流を制限するがback-powerを遮断しない。外部3.3 V Highを基板OFF中に印加した場合、1入力当たり概算0.6 mA以下、5入力合計で概算3 mA以下に制限されるが、3.3 V railやMCUの部分給電状態は機能保証されない。初回試作は追加isolationを実装せず、次を必須運用条件とする。

1. J2～J8は全機器の電源OFF中に接続・切断し、hot-plugしない。
2. 入力を駆動するUM982、Jetson、AUXより先、または同時に本基板へ給電する。本基板だけを先にOFFしない。
3. 接続先をOFFする前に`PPS_OE_N`と各`*_TX_OE_N`をHighへして全出力をHigh-Zにする。
4. firmware初期化完了までは全`OE_N`をHighに保ち、接続先の電源ON確認後に必要channelだけenableする。

この条件を守れない用途、hot-plug、量産版では、U4のIoff対応品への変更および接続先power sense/isolationを再検討する。

## DS3231 U2 pin割当

| U2 pin | 名称 | 接続・処理 |
|---:|---|---|
| 1 | 32kHz | `RTC_32K`、R26 10 kOhmで`+3V3`へpull-up、MCU PB10 |
| 2 | VCC | `+3V3`、C2 100 nFをpin直近へ配置 |
| 3 | INT/SQW | `RTC_INT_N`、R25 10 kOhmで`+3V3`へpull-up、MCU PC13 |
| 4 | RST | 未使用、open。内部50 kOhm nominal pull-upを使用し、外付けpull-up禁止 |
| 5～12 | N.C. | メーカー指定に従いすべてGNDへ接続 |
| 13 | GND | GND |
| 14 | VBAT | BT1 CR2032正極。一次電池backup用途のため外付けcapacitorなし |
| 15 | SDA | `RTC_SDA`、R4 4.7 kOhmで`+3V3`へpull-up、MCU PB7 |
| 16 | SCL | `RTC_SCL`、R3 4.7 kOhmで`+3V3`へpull-up、MCU PB6 |

## RESET / BOOT / DFU

- SW1 `RESET`は`NRST`とGND間。R42 10 kOhmで`NRST`を`+3V3`へpull-upし、C18 100 nFを`NRST`-GND間へ配置する。
- SW2 `BOOT`は`+3V3`からR44 1 kOhmを介してPA14-BOOT0へ接続する。R43 100 kOhmでPA14-BOOT0をGNDへpull-downする。
- SW1/SW2は小型Basic `TS-1088-AR02016` / C720477、SMD 4 x 3 mmを使用する。
- PA14はSWCLK兼用である。SWD probe接続中はSW2を押さず、BOOT操作時はprobeを外す。
- 製造時にoption byteを`nBOOT_SEL=0`、`nBOOT1=1`、`BOOT_LOCK=0`として外部BOOT0とsystem-memory bootを有効化する（AN2606の一般表では`nBOOT_SEL`を`nBOOT0_SEL`と表記）。通常起動はBOOT Low、system-memory USB DFUはSW2を押したままSW1を押して離し、その後SW2を離す。
- PA15の専用`DFU_REQ_N` buttonは設けない。正常firmwareからはUSB CDC commandによるsystem bootloader移行を実装し、firmware異常時はSW1/SW2を使用する。
- SWD recovery用TP1～TP5（SWDIO、SWCLK、NRST、3V3、GND）は維持する。
