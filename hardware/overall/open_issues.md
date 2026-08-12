# 未解決事項・リリース条件

- 更新日: 2026-08-13
- 対象: 初回試作
- 状態: DR-050で24 MHz C22434890とcomparatorを採用してOI-010を更新し、旧OI-011のbuffer不要判断を置換した。R-013は試作実測待ちである。残る判断待ちはOI-001/OI-003/OI-006/OI-009である。

## 判断待ち

| ID | 項目 | 現在の前提 | 完了期限 |
|---|---|---|---|
| OI-001 | J2～J8ハーネスの相手機器側connector/pin、線色、線長 | 基板側pinだけ確定。TBDを推測で埋めない | 試作ハーネス製作前 |
| OI-003 | 取付穴と最大部品高さ | 外形75 mm x 50 mm。穴は筐体条件取得まで配置しない | PCB外形・配置release前 |
| OI-006 | 出力テストポイントの点数と配置 | 設計書§4.1は出力別テストポイントを要求し、§8はPPS間チャネルスキュー測定を合否条件にしているが、BOMのTPはSWD用5点だけ（DR-019）。PPS 6出力と`PPS_GENERATED`に加え、J4の1.5 kOhm負荷を仮付けできるパッドを設ける。テストパッドはPCB図形で部品費は発生しない | PCB外形・配置release前 |
| OI-009 | VBUS側bulk容量の値とC11/C12/C13の割当 | USB 2.0はdevice側VBUS容量を10 uFまでとし、現BOMは10 uF x 3を「VBUS/3V3/LDO」と記載するだけで割当が未確定（DR-022）。R36の0 Ohmは突入電流を制限しない。**推奨: VBUS側を4.7 uFとして上限に余裕を持たせ、3.3 V側とLDO出力に10 uFを置く。** どのcapがどのnetかを回路図で確定させる | 回路図入力前 |

## 解決済み

| ID | 決定 | 根拠 |
|---|---|---|
| OI-002 | DR-033で旧DS3231 32 kHz captureを廃止し、TCXOをPF0/HSE bypassへ入力する | timer clock自体を高安定度化し、CH3 captureと比率推定を不要にする。DR-050では24 MHz ±1.5 ppm品をcomparatorでCMOS化する |
| OI-004 | DR-033/DR-050でPF0を`CLK_24M`、PF1をNC、PC14/PC15をLSEへ確定する | HSE bypass TCXOと内蔵RTC/LSEによりholdoverとbattery-backed calendarを分離する |
| OI-005 | 「RTC電池状態」は電圧値ではなく`RTC_VALID/INVALID`とする | 使用する全backup registerのsignature/versionとapplication payload CRC、RTC-domain configuration CRC、`INITS`、RTCSEL/LSEON/LSERDY/RTCEN、calendar範囲、SSR単調性から判定する。power-on時の不整合はES0548 §2.2.11のBDRST sequenceで初期化する。これはVBAT期間中のLSE連続動作や電池残量を保証しない。ADC分圧は追加しない |
| OI-007 | VLPのR15/R16は10 Ohm、その他の出力は33 Ohmとする | VLPは初回実力評価とし、10 Ohmで不要な電圧降下を小さくする |
| OI-008 | 初回は3.3 V `74LVC125A`出力の実測判定とし、専用driverを追加しない | 1.5 kOhm負荷とVLP実機で判定し、不合格なら次版でdriverを再設計する。未規定動作点の補間値は保証根拠に使わない |
| OI-010 | YXC `OW7EL89CENUYO3YLC-24M` / C22434890をU2、TI `LMV7219M7X/NOPB` / C544337をU14へ採用する | 24 MHz、clipped sine 0.8 Vp-p min、-30～85 ℃で±1.5 ppm。C19 AC結合と1.10 V bias後、U14で3.3 V CMOS化する。通常SMT、clock鎖約$1.83。根拠は[`../reviews/oi010_tcxo_selection.md`](../reviews/oi010_tcxo_selection.md) |
| OI-011 | 専用clock LDOは削除し、U14 comparatorとC19/C20/C21/R48-R50を使用する | DR-050で旧「buffer不要」を撤回。clipped-sine U2をPF0に適合させる最小回路とし、固定伝搬遅延は周波数へ累積しないことを試作確認する |

## EasyEDA入力前の必須確認

- [ ] MCU symbolのpin番号を[`pin_assignment.md`](./pin_assignment.md)の表と1本ずつ突き合わせる。**旧版の表はpin 1～32と40～41が実物と不一致だったため、表を信用してsymbol確認を省略しない。**
- [ ] pin 8=VDD、pin 9=VSS、pin 12=PF2-NRSTであることを確認する。
- [ ] pin 40=PD8、pin 41=PD9が電源netへ接続されていないことを確認する。`VDDIO2`という名前のpinは本パッケージに存在しない。
- [ ] pin 3=PC13（NC）、pin 4/5=PC14/PC15（X1 LSE）、pin 10=PF0（`CLK_24M`）、pin 11=PF1（NC）、pin 26=PC5（`PPS_GENERATED`）であることを確認する。
- [ ] `+3V3_A`を使用せず、VREF+を`+3V3`へ直結して100 nFを置く。pin 8は`VDD/VDDA`、pin 9は`VSS/VSSA`が正しい名称であり、VDDA専用decouplingは不要。
- [ ] BOMのMCU MPNが`STM32G0B1RBT6`（**N接尾辞なし**）であることを確認する。N接尾辞品はpin 40/41が`VSS`/`VDDIO2`で非互換。
- [ ] 解決済みOI-002/OI-004/OI-005/OI-007/OI-008/OI-010/OI-011と、未解決OI-006/OI-009の結果を反映する。
- [ ] U2=C22434890、U14=C544337、C19/C20/C21、R48/R49/R50を配置し、旧C48677994/C51822852/C51966238およびU15/`+2V8_CLK`を配置しない。

## EasyEDA設計後の確認

- [ ] symbol、footprint、pad番号を各メーカーの推奨land patternと照合する。
- [ ] U7/U8/U12だけでJ3～J8の全12出力を構成し、U7C/U7DがJ4へ接続されていることを確認する。
- [ ] U2/U14/X1、R45/R47/R48-R50、C19-C23のsymbol、pin、footprintとclock/backup netを確認する。U2は個別仕様確認後にpin 1/2=GND、3=OUT、4=`+3V3`、U14はpin 1=OUT、2=GND、3=+IN、4=-IN、5=`+3V3`とする。
- [ ] X1 loop内に信号配線がなく、裏面ローカルGND plane、最寄りMCU GNDへの接続、GND guard ring、PC13 keep-awayがあることを確認する。
- [ ] GH信号ESD footprintが配置されていないことを確認する。
- [ ] ERC/DRCを実行し、意図を記録した除外以外をゼロにする。
- [ ] EasyEDA project archive、schematic PDF、BOM、CPL、Gerberを保存する。

## 発注前の確認

- [ ] JLCPCBの在庫、予約可能数、Basic/Promotional/Extended区分、assembly fixture費用を再確認する。
- [ ] F1/D1 DNP、R36実装の初回variantがBOM/CPLへ反映されていることを確認する。
- [ ] R/Cの採用品が値、許容差、定格、温度特性、package/footprint条件を満たすことを確認する。
- [ ] U2 C22434890、U14 C544337、X1 C97604の在庫・予約可能数とU2が通常SMT実装であることを再確認する。YSO510TP family表の2016品は26 MHz以上だがLCSCには24 MHz品と20 MHz品が登録されているため、品番固有仕様書またはYXC回答で24 MHz / 2016 / pinout / ±1.5 ppmを確認する。
- [ ] VBUS側bulk容量が10 uFを超えていないことを確認する（USB 2.0のdevice側上限、DR-022）。
- [ ] OI-001とOI-003を完了する。

## 一次資料の確認状況

2026-08-12に[`../datasheets/`](../datasheets/)へDS13560 Rev 6、RM0444 Rev 6、AN2606、74LVC125A、DS3231を取得して旧構成を確認した。DR-033でDS3231は選定解除したが、MCU/USB/output回路に関する下記結論は有効である。

- [x] DS13560 Table 44の`ACC_HSI16`。DR-015の結論は変更なし（ダイ温度1 K変化で約36 ms）。
- [x] DS13560 Table 12 note 4。対象はPA8/PB15/PD0/PD2で、PB1/PB2/PC5は対象外。DR-002適合。
- [x] `PWR_CR2`の`PVM_VDDIO2`。reset値は「IOs in isolation mode」。firmwareで`110`を設定する。
- [x] 空flash判定（RM0444 §2.5.4）。**初回書込み後は電源の入れ直しが必要**（system resetでは`EMPTY`がクリアされない）。
- [x] DS13560 Figure 8 / Table 2。旧pin表の出所はN接尾辞品のpinoutだった。**BOMのMPNをN接尾辞なしで固定する。**
- [x] 74LVC125A Rev 12。VLPの直接保証点がないことを確認し、実測判定へ確定（OI-007/OI-008）。
- [x] DS3231の`IBATT`と`IOL`（旧DR-031確認。DR-033で部品自体を選定解除）。

未確認のまま残るもの:

- [x] YXC YSO510TP family資料、C22434890のLCSC/API属性、TI LMV7219一次資料をDR-050で照合した。C48677994/C51822852/C51966238は選定解除し、U14 comparatorを維持した。
- [ ] Torex XC6206P332のメーカー個別datasheet（U3の電源budget/thermal確認）。
- [ ] UM982のPPSパルス幅・立上り時間・出力ドライブ能力、既定でNMEAが出るUART（OI-001の裏付け）。追加された`Unicore Reference Commands Manual`はコマンド仕様書であり、電気的仕様は含まない。

## 試作合否試験

- [ ] J4 PPS/NMEAを各1.5 kOhmでGNDへ負荷し、High >3.0 V、Low <1.2 Vを全基板で確認する。
- [ ] J4接続前にVLP-32C側GPS PULSE / GPS RECEIVE端子の開放電圧を測定し、5 Vへのpull-upがないことを確認する（DR-024）。
- [ ] VLP-32C実機でPPS lock、反転NMEA decode、UTC +1秒を確認する。
- [ ] Mid-360とVLP-32Cがholdover中のRMC=`V`でtimestamp追従を維持するか確認する。
- [ ] 0～40 ℃でC22434890/U14を使用し、GNSS消失後5分の時刻誤差が1 ms以下であることを確認する。仕様上の±1.5 ppmをpeak-to-peak適用したTCXO寄与は0.900 ms、48 MHz timer量子化は0.00625 ms、その他の許容は0.09375 ms以下であり、公開仕様だけでは十分なmarginを保証できない。
- [ ] GNSS復帰時に短パルス、二重パルス、欠落、秒の逆行がないことを確認する。
- [ ] GNSS追従中の出力PPS位相差が1 us以内であることを確認する。TIM2の約20.833 ns分解能に対し、U4A経路の固定offset（概算55～82 ns）を校正した後で判定する。
- [ ] TCXO start-up、U2 pin 3の振幅、U14入力common-mode、PF0の24 MHz振幅・duty・jitter、CSS fault遷移を0～40 ℃で確認する。
- [ ] LSEは0 ℃、40 ℃、CR2032単独給電で`Sf >= 3`、`DL < 0.5 uW`を確認する。LSE不起動は`RTC_INVALID`であり`FAULT_CLOCK`へ入らないことも確認する。
- [ ] 0～40 ℃でPLLQ=48 MHzを使用してUSB列挙する。HSI48/CRSへ依存しないことを確認する。
- [ ] RTC連続INIT、option byte安全書込み、Jetson frameのCRC/欠落/再送試験を行う。
- [ ] 文書化した電源順序でback-power、誤出力、意図しないresetがないことを確認する。
- [ ] 最大動作時のVBUS電流が100 mA未満、主電源OFF時VBAT電流が2.0 uA以下であることを確認し、3.3 V droop、U3表面温度を[`power_budget.md`](./power_budget.md)へ記録する。
