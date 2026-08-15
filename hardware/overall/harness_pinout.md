# 試作ハーネス割当・TBD

- 状態: 基板側J2～J8は定義済み、相手機器側connector/pinと線色はTBD
- 適用段階: 試作ハーネス製作前に確定する
- 基板側部品: CJT `A1257WR-S-3P` / `A1257WR-S-4P`
- ケーブル側部品: CJT `A1257H-3P` / `A1257H-4P`、端子`A1257-TP`

## 基板側の確定割当

| Ref | 接続先 | GH pin 1 | GH pin 2 | GH pin 3 | GH pin 4 |
|---|---|---|---|---|---|
| J2 | UM982系 | GND | `GNSS_PPS_IN` | `GNSS_NMEA_RX_IN` | - |
| J3 | Jetson | GND | `JETSON_PPS_OUT` | `JETSON_TX_OUT` | `JETSON_RX_IN` |
| J4 | VLP-32C | GND | `VLP_PPS_OUT` | `VLP_TX_OUT` | - |
| J5 | Mid-360 | GND | `MID360_PPS_OUT` | `MID360_TX_OUT` | - |
| J6 | Spresense IMU level board | GND | `IMU_PPS_OUT` | `IMU_TX_OUT` | - |
| J7 | AUX1 | GND | `AUX1_PPS_OUT` | `AUX1_TX_OUT` | `AUX1_RX_IN` |
| J8 | AUX2 | GND | `AUX2_PPS_OUT` | `AUX2_TX_OUT` | `AUX2_RX_IN` |

入力名は直列抵抗よりU4側のEasyEDAラベルを記載する。コネクタから直列抵抗までの短い区間は無名netだが、ハーネス信号の機能は変わらない。J6の`IMU_*`はSpresense IMU `CXD5602PWBIMU1J`向け系統を表し、接続先や3.3 V出力条件の変更ではない。

この割当はPCB回路の正本である。相手機器側が未確定でも回路設計を継続できるが、ハーネス製造前には下表を埋めて導通検査票としてreleaseする。

## 相手機器側TBD

| Ref | 確認が必要な情報 | 参考情報（採用確定ではない） | 状態 |
|---|---|---|---|
| J2 | 実際に使用するUM982 carrier/evaluation board型番、UART番号、connector型番/pin | 裸UM982はPPS=pin 34。UART TXはTXD1 pin 17、TXD2 pin 19、TXD3 pin 27の候補があり、設定とcarrier配線で決まる | TBD |
| J3 | Jetson側housing/contact、GND pin選択 | Developer Kit 40-pin headerはpin 7=PPS候補、pin 8=Jetson TX、pin 10=Jetson RX、GNDはpin 6または9 | TBD |
| J4 | VLP Interface Box使用有無、terminal/connector方式 | Interface Box使用時は`GROUND`、`GPS PULSE`、`GPS RECEIVE`が候補 | TBD |
| J5 | 公式splitter cable使用またはM12へ直接接続、mating connector型番 | Mid-360本体はpin 2/3=GND、pin 8=PPS input、pin 10=GPS UART input | TBD |
| J6 | 別基板のconnector型番とpin番号 | 本基板は3.3 VのGND/PPS/NMEAまでを提供 | TBD |
| J7/J8 | 接続機器、mating connector、RX/TX方向 | 未確定時は片端GH・片端バラ線の試験用ハーネスとする | TBD |
| 全体 | 線色、長さ、シールド、枝分かれ、label | 線色だけで識別せず両端にRef/pin labelを付ける | TBD |

## 製造前TODO

- [ ] J2のUM982実装形態、使用UART、相手connectorを確定する。
- [ ] J3～J6の相手connector型番とpin番号を確定する。
- [ ] J7/J8の接続先、またはバラ線試験用とすることを確定する。
- [ ] 線色、線長、AWG、圧着端子、label規則を確定する。
- [ ] 各ハーネスについて「GH pin―相手pin―信号名―方向」の一対一表を発行する。
- [ ] 電源線が混在する相手connectorでは、本基板GHへ電源を誤接続しないことを二者確認する。
- [ ] 完成品の全線導通、隣接pin短絡、GND、TX/RX方向を100 %検査する。

## 導通検査票テンプレート

| Harness ID | GH Ref/pin | 相手connector/pin | Signal | Direction | Wire color | Continuity | Adjacent short | Inspector |
|---|---|---|---|---|---|---|---|---|
| TBD | TBD | TBD | TBD | TBD | TBD | PASS / FAIL | PASS / FAIL | TBD |

相手機器側情報が確定するまでは、本書のTBDを推測で埋めない。
