# 未解決事項・リリース条件

- 更新日: 2026-08-12
- 対象: 初回試作
- 状態: [`../reviews/pre_easyeda_design_review.md`](../reviews/pre_easyeda_design_review.md)のDR-012～DR-030は正本文書へ反映済み。**回路の修正は完了しており、残るのは下記OI-001～OI-009の判断だけである。** 回路図入力の開始前に「EasyEDA入力前の必須確認」を完了する。

## 判断待ち

| ID | 項目 | 現在の前提 | 完了期限 |
|---|---|---|---|
| OI-001 | J2～J8ハーネスの相手機器側connector/pin、線色、線長 | 基板側pinだけ確定。TBDを推測で埋めない | 試作ハーネス製作前 |
| OI-002 | holdoverでDS3231のどちらの出力を周波数基準にするか（32 kHz常時計測 / 1 Hz `INT/SQW`） | **DS3231による常時規律自体は要求R-013の成立条件であり選択肢ではない**（DR-015）。回路はPB10/R26と PC13/R25の両方を持つ。どちらでも誤差予算は閉じるため、firmwareの実装順で決める | firmware baseline確定前 |
| OI-003 | 取付穴と最大部品高さ | 外形75 mm x 50 mm。穴は筐体条件取得まで配置しない | PCB外形・配置release前 |
| OI-004 | PF0/PF1へHSE水晶のfootprintを設けるか | **推奨: footprintを設けて初回DNP。** 現状これらは単なるtest padであり、水晶1個 + 負荷容量2個のlandを追加するコストはほぼない。DS3231規律が期待どおり動作しない場合の代替経路、UART baud誤差（DR-025）の解消、後日のTCXO差し替えに使える。USBは水晶を必要としない（HSI48 + CRS） | PCB外形・配置release前 |
| OI-005 | 「RTC電池状態」の定義 | **推奨: OSFおよびRTC読出し可否から導く二値情報へ再定義する（追加部品なし）。** VBAT実電圧を読む場合は1 MOhm x 2の分圧をADC入力（PC4候補）へ入れる。消費1.65 uAはCR2032 220 mAhに対し概算15年相当だが、DS3231自身の`IBAT` typ 0.84 uAと同等の追加負荷になる。ADC channel対応の確認が必要 | firmware baseline確定前 |
| OI-006 | 出力テストポイントの点数と配置 | 設計書§4.1は出力別テストポイントを要求し、§8はPPS間チャネルスキュー測定を合否条件にしているが、BOMのTPはSWD用5点だけ（DR-019）。PPS 6出力と`PPS_GENERATED`に加え、J4の1.5 kOhm負荷を仮付けできるパッドを設ける。テストパッドはPCB図形で部品費は発生しない | PCB外形・配置release前 |
| OI-007 | VLP出力（`VLP_PPS_OUT`、`VLP_NMEA_INV_OUT`）の直列抵抗値 | **推奨: 33 Ohm → 10 Ohm（R15/R16として分離済み）。** BOMのC-codeがTBDなのでBasic品から10 Ohm 0603を選定する。数値根拠はOI-008と共通 | 回路図入力前 |
| OI-008 | VLP-32C出力を実測判定に賭けるか、初回から専用driverを載せるか | DR-030で定量化した結果、74LVC125A・3.3 Vでは**直列抵抗を0 Ohmにしてもworst caseは約2.99 Vで要求>3.0 Vに届かない**（typicalは約3.18 Vで通る）。抵抗値では構造的に解消しない。**推奨: 現方針（実測判定＋不合格なら次版で専用driver）を維持する。** 初回から保証したい場合は4.5～4.8 V regulated railと専用buffer（削除したU5/U6/U9相当）が必要で、Extended部品とfeeder feeが復活する。VBUS 5 Vの直接駆動はVLPのHigh上限5.0 Vを超え得るため不可 | 回路図入力前 |
| OI-009 | VBUS側bulk容量の値とC11/C12/C13の割当 | USB 2.0はdevice側VBUS容量を10 uFまでとし、現BOMは10 uF x 3を「VBUS/3V3/LDO」と記載するだけで割当が未確定（DR-022）。R36の0 Ohmは突入電流を制限しない。**推奨: VBUS側を4.7 uFとして上限に余裕を持たせ、3.3 V側とLDO出力に10 uFを置く。** どのcapがどのnetかを回路図で確定させる | 回路図入力前 |

## EasyEDA入力前の必須確認

- [ ] MCU symbolのpin番号を[`pin_assignment.md`](./pin_assignment.md)の表と1本ずつ突き合わせる。**旧版の表はpin 1～32と40～41が実物と不一致だったため、表を信用してsymbol確認を省略しない。**
- [ ] pin 8=VDD、pin 9=VSS、pin 12=PF2-NRSTであることを確認する。
- [ ] pin 40=PD8、pin 41=PD9が電源netへ接続されていないことを確認する。`VDDIO2`という名前のpinは本パッケージに存在しない。
- [ ] pin 3=PC13（`RTC_INT_N`）、pin 26=PC5（`PPS_GENERATED`、TIM2_CH2）であることを確認する。
- [ ] `+3V3_A`を使用せず、VREF+を`+3V3`へ直結して100 nFを置く。pin 8は`VDD/VDDA`、pin 9は`VSS/VSSA`が正しい名称であり、VDDA専用decouplingは不要。
- [ ] BOMのMCU MPNが`STM32G0B1RBT6`（**N接尾辞なし**）であることを確認する。N接尾辞品はpin 40/41が`VSS`/`VDDIO2`で非互換。
- [ ] OI-004～OI-009の判断を反映する。

## EasyEDA設計後の確認

- [ ] symbol、footprint、pad番号を各メーカーの推奨land patternと照合する。
- [ ] U7/U8/U12だけでJ3～J8の全12出力を構成し、U7C/U7DがJ4へ接続されていることを確認する。
- [ ] GH信号ESD footprintが配置されていないことを確認する。
- [ ] ERC/DRCを実行し、意図を記録した除外以外をゼロにする。
- [ ] EasyEDA project archive、schematic PDF、BOM、CPL、Gerberを保存する。

## 発注前の確認

- [ ] JLCPCBの在庫、予約可能数、Basic/Promotional/Extended区分、assembly fixture費用を再確認する。
- [ ] F1/D1 DNP、R36実装の初回variantがBOM/CPLへ反映されていることを確認する。
- [ ] R/Cの採用品が値、許容差、定格、温度特性、package/footprint条件を満たすことを確認する。
- [ ] VBUS側bulk容量が10 uFを超えていないことを確認する（USB 2.0のdevice側上限、DR-022）。
- [ ] OI-001とOI-003を完了する。

## 一次資料の確認状況

2026-08-12に[`../datasheets/`](../datasheets/)へDS13560 Rev 6、RM0444 Rev 6、AN2606、74LVC125A、DS3231を取得し、下記を解消した（[`../reviews/pre_easyeda_design_review.md`](../reviews/pre_easyeda_design_review.md) §11）。

- [x] DS13560 Table 44の`ACC_HSI16`。DR-015の結論は変更なし（ダイ温度1 K変化で約36 ms）。
- [x] DS13560 Table 12 note 4。対象はPA8/PB15/PD0/PD2で、PB1/PB2/PC5は対象外。DR-002適合。
- [x] `PWR_CR2`の`PVM_VDDIO2`。reset値は「IOs in isolation mode」。firmwareで`110`を設定する。
- [x] 空flash判定（RM0444 §2.5.4）。**初回書込み後は電源の入れ直しが必要**（system resetでは`EMPTY`がクリアされない）。
- [x] DS13560 Figure 8 / Table 2。旧pin表の出所はN接尾辞品のpinoutだった。**BOMのMPNをN接尾辞なしで固定する。**
- [x] 74LVC125A Rev 12。VLP出力のworst caseを定量化（OI-007）。
- [x] DS3231の`IBATT`と`IOL`。`EN32kHz`を落とす処理は不要。

未確認のまま残るもの:

- [ ] PC4のADC channel対応（OI-005の案Bを採る場合のみ）。
- [ ] XC6206の出力電圧公差（OI-007の計算で±2 %を仮定）。Torexデータシート。
- [ ] UM982のPPSパルス幅・立上り時間・出力ドライブ能力、既定でNMEAが出るUART（OI-001の裏付け）。追加された`Unicore Reference Commands Manual`はコマンド仕様書であり、電気的仕様は含まない。

## 試作合否試験

- [ ] J4 PPS/NMEAを各1.5 kOhmでGNDへ負荷し、High >3.0 V、Low <1.2 Vを全基板で確認する。
- [ ] J4接続前にVLP-32C側GPS PULSE / GPS RECEIVE端子の開放電圧を測定し、5 Vへのpull-upがないことを確認する（DR-024）。
- [ ] VLP-32C実機でPPS lock、反転NMEA decode、UTC +1秒を確認する。
- [ ] Mid-360とVLP-32Cがholdover中のRMC=`V`でtimestamp追従を維持するか確認する。
- [ ] 0～40 ℃でGNSS消失後5分の時刻誤差が1 ms以下であることを確認する。**DS3231規律を無効にした状態では成立しないため、規律ありの構成で測定する**（DR-015）。
- [ ] GNSS復帰時に短パルス、二重パルス、欠落、秒の逆行がないことを確認する。
- [ ] GNSS追従中の出力PPS位相差が1 us以内であることを確認する。TIM2の15.625 ns分解能に対し、U4A経路の固定offset（概算55～82 ns）を校正した後で判定する。
- [ ] 文書化した電源順序でback-power、誤出力、意図しないresetがないことを確認する。
- [ ] 最大動作時の3.3 V消費電流、電圧droop、U3 XC6206の表面温度を測定する（DR-023）。
