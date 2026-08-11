# 未解決事項・リリース条件

- 更新日: 2026-08-11
- 対象: 初回試作
- 状態: 回路設計を止めるblockerはない。該当工程へ進む前に下記を完了する。

## 判断待ち

| ID | 項目 | 現在の前提 | 完了期限 |
|---|---|---|---|
| OI-001 | J2～J8ハーネスの相手機器側connector/pin、線色、線長 | 基板側pinだけ確定。TBDを推測で埋めない | 試作ハーネス製作前 |
| OI-002 | RTC 32 kHz常時補正を初回firmwareへ含めるか | PB10/R26の回路は残す。まずGNSS DPLLとRTC 1 Hzで5分holdoverを測定する | firmware baseline確定前 |
| OI-003 | 取付穴と最大部品高さ | 外形75 mm x 50 mm。穴は筐体条件取得まで配置しない | PCB外形・配置release前 |

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
- [ ] OI-001とOI-003を完了する。

## 試作合否試験

- [ ] J4 PPS/NMEAを各1.5 kOhmでGNDへ負荷し、High >3.0 V、Low <1.2 Vを全基板で確認する。
- [ ] VLP-32C実機でPPS lock、反転NMEA decode、UTC +1秒を確認する。
- [ ] Mid-360とVLP-32Cがholdover中のRMC=`V`でtimestamp追従を維持するか確認する。
- [ ] 0～40 ℃でGNSS消失後5分の時刻誤差が1 ms以下であることを確認する。不合格ならOI-002の32 kHz補正を有効化して再評価する。
- [ ] GNSS復帰時に短パルス、二重パルス、欠落、秒の逆行がないことを確認する。
- [ ] 文書化した電源順序でback-power、誤出力、意図しないresetがないことを確認する。
