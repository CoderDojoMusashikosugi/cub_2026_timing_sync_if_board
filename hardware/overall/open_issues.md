# 未解決事項・リリース条件

- 更新日: 2026-08-15
- 対象: 初回試作
- 状態: DR-057で第2回EasyEDA実回路図を正本へ同期し、DR-058でBT1極性を確認、DR-059でU3を逆電流保護付きTPS70933DBVRへ置換してOI-012を解決した。R-013は試作実測待ちであり、残る判断待ちはOI-001/OI-003/OI-014である。

## 判断待ち

| ID | 項目 | 現在の前提 | 完了期限 |
|---|---|---|---|
| OI-001 | J2～J8ハーネスの相手機器側connector/pin、線色、線長 | 基板側pinだけ確定。TBDを推測で埋めない | 試作ハーネス製作前 |
| OI-003 | 取付穴と最大部品高さ | 外形75 mm x 50 mm。穴は筐体条件取得まで配置しない | PCB外形・配置release前 |
| OI-014 | U3置換後のEasyEDA成果物・DRC確認 | U3は`TPS70933DBVR` / C89347へ置換済みとの報告を受け、正本文書と予備BOMをDR-059で更新した。一方、`review_20260815_02/materials`の`.epro2`、`.enet`、PDF、BOM、DRCログは旧`TPS7A2433DBVR`のまま。再export後にMPN、C-code、SOT-23-5 pin 1～5、`VBUS_PROTECTED`/`3V3`接続、DRC 0件を照合する | PCB/BOM/CPL release前 |

## 解決済み

| ID | 決定 | 根拠 |
|---|---|---|
| OI-002 | DR-033で旧DS3231 32 kHz captureを廃止し、TCXOをPF0/HSE bypassへ入力する | timer clock自体を高安定度化し、CH3 captureと比率推定を不要にする。DR-050では24 MHz ±1.5 ppm品をcomparatorでCMOS化する |
| OI-004 | DR-033/DR-050でPF0を`CLK_24M`、PF1をNC、PC14/PC15をLSEへ確定する | HSE bypass TCXOと内蔵RTC/LSEによりholdoverとbattery-backed calendarを分離する |
| OI-005 | 「RTC電池状態」は電圧値ではなく`RTC_VALID/INVALID`とする | 使用する全backup registerのsignature/versionとapplication payload CRC、RTC-domain configuration CRC、`INITS`、RTCSEL/LSEON/LSERDY/RTCEN、calendar範囲、SSR単調性から判定する。power-on時の不整合はES0548 §2.2.11のBDRST sequenceで初期化する。これはVBAT期間中のLSE連続動作や電池残量を保証しない。ADC分圧は追加しない |
| OI-006 | DR-055でテストパッドをTP1～TP9へ確定する | TP1～TP5=SWD、TP6=`PPS_GENERATED`、TP7=`VLP_PPS_OUT`、TP8=`VLP_TX_OUT`、TP9=`TP_FW_TRACE`。TP9はPA6に接続する唯一の汎用firmware trace padとし、SWOには使わない。通常のPPS出力は測定用ハーネスで各コネクタpin 2から測り、pin 1をGNDとする。その他のstatus/spare/debug GPIOおよび通常PPS出力の重複パッドは設けない |
| OI-007 | VLPの現行R21/R23は10 Ohm、その他の出力は33 Ohmとする | VLPは初回実力評価とし、10 Ohmで不要な電圧降下を小さくする。旧RefDes R15/R16はDR-057で置換済み |
| OI-008 | 初回は3.3 V `74LVC125A`出力の実測判定とし、専用driverを追加しない | 1.5 kOhm負荷とVLP実機で判定し、不合格なら次版でdriverを再設計する。未規定動作点の補間値は保証根拠に使わない |
| OI-009 | DR-057でVBUS側C1=4.7 uF、3V3側C2/C3=各10 uFへ確定する | 第2回EasyEDA実回路図・netlist・BOMで値とnetを照合済み。VBUS側は10 uF未満であり、R3の0 Ohmは突入電流を制限しない |
| OI-010 | YXC `OW7EL89CENUYO3YLC-24M` / C22434890をU2、TI `LMV7219M7X/NOPB` / C544337をU14へ採用する | 24 MHz、clipped sine 0.8 Vp-p min、-30～85 ℃で±1.5 ppm。C11 AC結合と1.031 V bias後、U14で3.3 V CMOS化する。通常SMT、clock鎖価格は発注時再確認。根拠は[`../reviews/oi010_tcxo_selection.md`](../reviews/oi010_tcxo_selection.md) |
| OI-011 | 専用clock LDOは削除し、U14 comparatorとC11/C12/C13/R5-R8を使用する | DR-050で旧「buffer不要」を撤回。clipped-sine U2をPF0に適合させる最小回路とし、固定伝搬遅延は周波数へ累積しないことを試作確認する |
| OI-012 | DR-059でU3を逆電流保護付き`TPS70933DBVR` / C89347へ置換する | TPS709は逆電流保護を内蔵し、`VOUT=3 V、VIN=VEN=0 V`でIN pinから流出する逆電流typ 10 nA、OUT pinへ流入する電流typ 100 nA。旧TPS7A24の`VOUT > VIN`時ボディダイオード課題を部品置換で解消した。USB切断・再投入試験は機能確認として継続する |
| OI-013 | DR-058でBT1 `BS-CR2032-8` / C7498149をpin 1=`-`/GND、pin 2=`+`/R4・`VBAT_RTC`側と確定する | 受領`.epro2`内のfootprint polarity表示、LCSC公開EasyEDA footprint、受領netlistの3者が一致した。根拠は[`../sch_reviews/review_20260815_02/Codex/bt1_polarity_verification.md`](../sch_reviews/review_20260815_02/Codex/bt1_polarity_verification.md) |

## EasyEDA入力前の必須確認

- [ ] MCU symbolのpin番号を[`pin_assignment.md`](./pin_assignment.md)の表と1本ずつ突き合わせる。**旧版の表はpin 1～32と40～41が実物と不一致だったため、表を信用してsymbol確認を省略しない。**
- [ ] pin 8=VDD、pin 9=VSS、pin 12=PF2-NRSTであることを確認する。
- [ ] pin 40=PD8、pin 41=PD9が電源netへ接続されていないことを確認する。`VDDIO2`という名前のpinは本パッケージに存在しない。
- [ ] pin 3=PC13（NC）、pin 4/5=PC14/PC15（X1 LSE）、pin 10=PF0（`CLK_24M`）、pin 11=PF1（NC）、pin 26=PC5（`PPS_GENERATED`）であることを確認する。
- [ ] 旧`+3V3_A`を使用せず、VREF+を`3V3`へ直結して100 nFを置く。pin 8は`VDD/VDDA`、pin 9は`VSS/VSSA`が正しい名称であり、VDDA専用decouplingは不要。
- [ ] BOMのMCU MPNが`STM32G0B1RBT6`（**N接尾辞なし**）であることを確認する。N接尾辞品はpin 40/41が`VSS`/`VDDIO2`で非互換。
- [ ] 解決済みOI-002/OI-004/OI-005/OI-006/OI-007/OI-008/OI-009/OI-010/OI-011/OI-012/OI-013と、未解決OI-014の結果を反映する。
- [ ] U2=C22434890、U14=C544337、C11/C12/C13、R5/R6/R7/R8を配置し、旧C48677994/C51822852/C51966238およびU15/`+2V8_CLK`を配置しない。

## EasyEDA設計後の確認

- [ ] symbol、footprint、pad番号を各メーカーの推奨land patternと照合する。
- [ ] U7/U8/U12だけでJ3～J8の全12出力を構成し、U7C/U7DがJ4へ接続されていることを確認する。
- [ ] U2/U14/X1、R4-R8、C8/C11-C15のsymbol、pin、footprintとclock/backup netを確認する。U2は個別仕様確認後にpin 1=NC、2=GND、3=OUT、4=`3V3`、U14はpin 1=OUT、2=GND、3=+IN、4=-IN、5=`3V3`とする。
- [ ] X1 loop内に信号配線がなく、裏面ローカルGND plane、最寄りMCU GNDへの接続、GND guard ring、PC13 keep-awayがあることを確認する。
- [ ] GH信号ESD footprintが配置されていないことを確認する。
- [ ] ERC/DRCを実行し、意図を記録した除外以外をゼロにする。
- [ ] EasyEDA project archive、schematic PDF、BOM、CPL、Gerberを保存する。

## 発注前の確認

- [ ] JLCPCBの在庫、予約可能数、Basic/Promotional/Extended区分、assembly fixture費用を再確認する。
- [ ] F1 DNP、D1 `SMF6.0A` / C19077499とR3実装の初回variantがBOM/CPLへ反映され、R36が`IMU_TX_OE_N` pull-upであることを確認する。
- [ ] R/Cの採用品が値、許容差、定格、温度特性、package/footprint条件を満たすことを確認する。
- [ ] U2 C22434890、U14 C544337、X1 C97604の在庫・予約可能数とU2が通常SMT実装であることを再確認する。YSO510TP family表の2016品は26 MHz以上だがLCSCには24 MHz品と20 MHz品が登録されているため、品番固有仕様書またはYXC回答で24 MHz / 2016 / pinout / ±1.5 ppmを確認する。
- [ ] U3=`TPS70933DBVR` / C89347、SOT-23-5、pin 1=IN、2=GND、3=EN、4=NC、5=OUTであることを置換後EasyEDA成果物とJLCPCB parts libraryで確認する（DR-059、OI-014）。
- [ ] VBUS側C1=4.7 uF、3V3側C2/C3=各10 uFであることを確認する（DR-057）。
- [ ] OI-001、OI-003、OI-014を完了する。

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
- [x] TI TPS709およびSN74LV14Aのメーカーdatasheet。U3 pinout/電圧/熱特性/逆電流保護をDR-059、U4 3.3 Vしきい値/遅延/`Ioff`をDR-057で反映した。旧TPS7A24資料はDR-057～DR-059間の履歴根拠として保持する。
- [x] BT1 `BS-CR2032-8` / C7498149のpin極性。DR-058で受領`.epro2`内footprintとLCSC公開EasyEDA footprintを照合し、pin 1=`-`、pin 2=`+`を確認した。
- [ ] UM982のPPSパルス幅・立上り時間・出力ドライブ能力、既定でNMEAが出るUART（OI-001の裏付け）。追加された`Unicore Reference Commands Manual`はコマンド仕様書であり、電気的仕様は含まない。

## 試作合否試験

- [ ] J4 PPS/NMEAを各1.5 kOhmでGNDへ負荷し、High >3.0 V、Low <1.2 Vを全基板で確認する。
- [ ] J4接続前にVLP-32C側GPS PULSE / GPS RECEIVE端子の開放電圧を測定し、5 Vへのpull-upがないことを確認する（DR-024）。
- [ ] VLP-32C実機でPPS lock、反転NMEA decode、UTC +1秒を確認する。
- [ ] Mid-360とVLP-32Cがholdover中のRMC=`V`でtimestamp追従を維持するか確認する。
- [ ] 0～40 ℃でC22434890/U14を使用し、GNSS消失後5分の時刻誤差が1 ms以下であることを確認する。仕様上の±1.5 ppmをpeak-to-peak適用したTCXO寄与は0.900 ms、48 MHz timer量子化は0.00625 ms、その他の許容は0.09375 ms以下であり、公開仕様だけでは十分なmarginを保証できない。
- [ ] GNSS復帰時に短パルス、二重パルス、欠落、秒の逆行がないことを確認する。
- [ ] GNSS追従中の出力PPS位相差が1 us以内であることを確認する。TIM2の約20.833 ns分解能に対し、U4A経路の固定offsetを実測・校正した後で判定する。
- [ ] TCXO start-up、U2 pin 3の振幅、U14入力common-mode、PF0の24 MHz振幅・duty・jitter、CSS fault遷移を0～40 ℃で確認する。
- [ ] LSEは0 ℃、40 ℃、CR2032単独給電で`Sf >= 3`、`DL < 0.5 uW`を確認する。LSE不起動は`RTC_INVALID`であり`FAULT_CLOCK`へ入らないことも確認する。
- [ ] 0～40 ℃でPLLQ=48 MHzを使用してUSB列挙する。HSI48/CRSへ依存しないことを確認する。
- [ ] RTC連続INIT、option byte安全書込み、Jetson frameのCRC/欠落/再送試験を行う。
- [ ] 文書化した電源順序でback-power、誤出力、意図しないresetがないことを確認する。
- [ ] USB切断時の`VBUS_PROTECTED`/`3V3`波形と再投入時の起動を測定し、TPS709の逆電流保護を含む電源遷移が正常であることを機能確認する。OI-012自体はDR-059の部品置換で解決済み。
- [ ] 最大動作時のVBUS電流が100 mA未満、主電源OFF時VBAT電流が2.0 uA以下であることを確認し、3.3 V droop、U3表面温度を[`power_budget.md`](./power_budget.md)へ記録する。
