# 未解決事項・リリース条件

- 更新日: 2026-10-10
- 対象: 初回試作
- 状態: 2026-09-06に発注した（DR-060でMCUを`STM32G0B1RCT6` / `C3034735`へ変更）。2026-10-03から2枚（UID `…000F0038`、`…000E0038`）で試験しており、試験の進め方は[`../../firmware/test_plan.md`](../../firmware/test_plan.md)、結果は基板ごとに[`../../firmware/test_results/`](../../firmware/test_results/)へ記録する。残る判断待ちはOI-001/OI-003/OI-016/OI-018である。GNSSの受信（OI-017）は2026-10-10に解決し、屋外で`GNSS_LOCKED`まで確認した。R-013は試作実測待ちである。

## 判断待ち

| ID | 項目 | 現在の前提 | 完了期限 |
|---|---|---|---|
| OI-001 | J2～J8ハーネスの相手機器側connector/pin、線色、線長 | 基板側pinだけ確定。2026-10-10に試作ハーネスでJ2～J8を各機器へつないで試験したが、相手側のpin・線色・線長は[`harness_pinout.md`](./harness_pinout.md)へ記録していない。TBDを推測で埋めない | 実機統合試験（test_plan P6）前 |
| OI-003 | 取付穴と最大部品高さ | 発注版のPCBには取付穴がない（[round3 checklist](../../pcb_review/round3/claude_code/review_checklist.md)）。筐体条件は未取得 | 筐体設計・次版PCB前 |
| OI-016 | 発注版PCBと正本・EasyEDA原本の不一致 | 発注時の最終成果物（`pcb_review/round3/materials`）は4層・60 × 38 mmで、[`design.md`](../../plan/design.md) §6.7、DR-023、[`pcb_layout_guidelines.md`](./pcb_layout_guidelines.md)の2層・75 × 50 mmと合わない（PCB-CX-002）。PCB-CX-003～006（LSE直下のGND、TPの面、F1/R3の排他表示、コネクタ間隔）も未解決のまま発注した（[round3 review](../../pcb_review/round3/codex/review_result.md)）。EasyEDAのBOM exportはU1が旧`STM32G0B1RBT6` / `C5143679`のまま（実装品は`RCT6`、OI-015）で、PCB DRCのログは保存していない。実物の外形・層数を確かめ、正本を発注版に合わせるか、次版で正本に戻すかを決める | 次版PCB前 |
| OI-018 | 基板のUSBが列挙されたまま応答しなくなる | 2026-10-10 16:00～16:03にUID `…000E0038`で1回起きた。COM8・COM9とも開くと戻らず、電源の入れ直しで戻った。DFUが現れないのでHardFaultではなく、main loopが止まった疑い。J2には何も届いていなかったのでGNSSの受信は関係ない。17:55～18:13にも記録が途切れたが、PCの蓋を閉じたためかは不明。原因は未特定（[試験記録](../../firmware/test_results/2036324356305018000E0038/summary.md)） | release前 |

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
| OI-014 | U3は`TPS70933DBVR` / C89347で発注・実装した | 発注版の回路図・両netlist・両BOMで品番、C-code、pin 1～5の接続を照合した（[PCB round1 review](../../pcb_review/round1/codex/review_result.md)）。実基板で`3V3`=3.288 V（BU-03、UID `…000F0038`）。PCB DRCログがない点はOI-016へ移した |
| OI-015 | U1は`STM32G0B1RCT6`が実装された | 2枚ともfirmwareの`id`で`DEV_ID` 0x467、flash容量register 256 KB（`RBT6`なら128 KB）。EasyEDAのBOM exportが旧品番のままである点はOI-016へ移した |
| OI-017 | UM982からJ2へ信号が来なかった原因は、ハーネスのUM982側でTXとRXを取り違えていたこと | 2026-10-10にUID `…000E0038`で、UM982 2台のCOM1～COM3すべてへRMCを出してもJ2が無反応なことから配線を疑い、直した後はUM982のCOM3（115200）から受信できた。屋外でPPSとRMC=Aがそろってから8秒で`GNSS_LOCKED`、lock中175秒の位相差はσ 20 ns、最大62 ns。途中で見つかったfirmwareの不具合3件（測位後のRMCが長すぎて捨てられる、TCXO偏差の換算が1000倍、空の秒を数えてgoodが0に戻る）は修正済み（[試験記録](../../firmware/test_results/2036324356305018000E0038/summary.md)） |

## 初回試作の発注前チェック（履歴）

次の3つのリストは、初回試作の発注（2026-09-06）までに使ったものである。確認の記録は[`../../pcb_review/`](../../pcb_review/)のround1～3にあり、チェック欄は当時から更新していない。発注版に残った不一致はOI-016にまとめた。次版の発注前に、この3つのリストを見直して使う。

### EasyEDA入力前の必須確認

- [ ] MCU symbolのpin番号を[`pin_assignment.md`](./pin_assignment.md)の表と1本ずつ突き合わせる。**旧版の表はpin 1～32と40～41が実物と不一致だったため、表を信用してsymbol確認を省略しない。**
- [ ] pin 8=VDD、pin 9=VSS、pin 12=PF2-NRSTであることを確認する。
- [ ] pin 40=PD8、pin 41=PD9が電源netへ接続されていないことを確認する。`VDDIO2`という名前のpinは本パッケージに存在しない。
- [ ] pin 3=PC13（NC）、pin 4/5=PC14/PC15（X1 LSE）、pin 10=PF0（`CLK_24M`）、pin 11=PF1（NC）、pin 26=PC5（`PPS_GENERATED`）であることを確認する。
- [ ] 旧`+3V3_A`を使用せず、VREF+を`3V3`へ直結して100 nFを置く。pin 8は`VDD/VDDA`、pin 9は`VSS/VSSA`が正しい名称であり、VDDA専用decouplingは不要。
- [ ] BOMのMCU MPNが`STM32G0B1RCT6` / `C3034735`（**N接尾辞なし**）であることを確認する（DR-060で`STM32G0B1RBT6` / `C5143679`から変更）。N接尾辞品はpin 40/41が`VSS`/`VDDIO2`で非互換。GP版の`STM32G0B1RBT6` / `RCT6` / `RET6`は相互にpin互換であり、在庫都合の移動ではpin表・PCBを変更しない。
- [ ] 解決済みOI-002/OI-004/OI-005/OI-006/OI-007/OI-008/OI-009/OI-010/OI-011/OI-012/OI-013と、未解決OI-014の結果を反映する。
- [ ] U2=C22434890、U14=C544337、C11/C12/C13、R5/R6/R7/R8を配置し、旧C48677994/C51822852/C51966238およびU15/`+2V8_CLK`を配置しない。

### EasyEDA設計後の確認

- [ ] symbol、footprint、pad番号を各メーカーの推奨land patternと照合する。
- [ ] U7/U8/U12だけでJ3～J8の全12出力を構成し、U7C/U7DがJ4へ接続されていることを確認する。
- [ ] U2/U14/X1、R4-R8、C8/C11-C15のsymbol、pin、footprintとclock/backup netを確認する。U2は個別仕様確認後にpin 1=NC、2=GND、3=OUT、4=`3V3`、U14はpin 1=OUT、2=GND、3=+IN、4=-IN、5=`3V3`とする。
- [ ] X1 loop内に信号配線がなく、裏面ローカルGND plane、最寄りMCU GNDへの接続、GND guard ring、PC13 keep-awayがあることを確認する。
- [ ] GH信号ESD footprintが配置されていないことを確認する。
- [ ] ERC/DRCを実行し、意図を記録した除外以外をゼロにする。
- [ ] EasyEDA project archive、schematic PDF、BOM、CPL、Gerberを保存する。

### 発注前の確認

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
- [x] 空flash判定（RM0444 §2.5.4）。**初回書込み後は電源の入れ直しが必要**（system resetでは`EMPTY`がクリアされない）。現行firmwareは起動時に`EMPTY`を0にしてresetし直すため、入れ直しは不要になった（BU-06、2026-10-03。[`../../firmware/README.md`](../../firmware/README.md)）。
- [x] DS13560 Figure 8 / Table 2。旧pin表の出所はN接尾辞品のpinoutだった。**BOMのMPNをN接尾辞なしで固定する。**
- [x] 74LVC125A Rev 12。VLPの直接保証点がないことを確認し、実測判定へ確定（OI-007/OI-008）。
- [x] DS3231の`IBATT`と`IOL`（旧DR-031確認。DR-033で部品自体を選定解除）。

未確認のまま残るもの:

- [x] YXC YSO510TP family資料、C22434890のLCSC/API属性、TI LMV7219一次資料をDR-050で照合した。C48677994/C51822852/C51966238は選定解除し、U14 comparatorを維持した。
- [x] TI TPS709およびSN74LV14Aのメーカーdatasheet。U3 pinout/電圧/熱特性/逆電流保護をDR-059、U4 3.3 Vしきい値/遅延/`Ioff`をDR-057で反映した。旧TPS7A24資料はDR-057～DR-059間の履歴根拠として保持する。
- [x] BT1 `BS-CR2032-8` / C7498149のpin極性。DR-058で受領`.epro2`内footprintとLCSC公開EasyEDA footprintを照合し、pin 1=`-`、pin 2=`+`を確認した。
- [ ] UM982のPPSパルス幅・立上り時間・出力ドライブ能力、既定でNMEAが出るUART（OI-001の裏付け）。追加された`Unicore Reference Commands Manual`はコマンド仕様書であり、電気的仕様は含まない。

## 試作合否試験

各項目の試験IDと手順は[`../../firmware/test_plan.md`](../../firmware/test_plan.md)、基板ごとの結果は[`../../firmware/test_results/`](../../firmware/test_results/)の`summary.md`にある。

- [ ] J4 PPS/NMEAを各1.5 kOhmでGNDへ負荷し、High >3.0 V、Low <1.2 Vを全基板で確認する。
- [x] VLP-32C側GPS PULSE / GPS RECEIVE端子の開放電圧を測定し、5 Vへのpull-upがないことを確認する（DR-024）。2026-10-10、どの端子も0 Vまたは浮遊で、pull-upはない。浮遊の端子は、本基板の出力がHigh-Zの間（起動直後、step中、`out off`）入力が定まらない。そのときVLP-32CがPPSやNMEAを誤って受けないかを、SY-02で確かめる。
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
