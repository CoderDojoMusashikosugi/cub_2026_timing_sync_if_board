# 初回試作基板（2026-09-06発注）

2026-09-06にJLCPCB（PCBA）へ発注した初回試作の、EasyEDA Proからの出力。実物の基板はこの内容で作られている。2026-10からこの基板2枚で試験している。

| ファイル | 内容 |
|---|---|
| [`schematic_2026-09-06.pdf`](./schematic_2026-09-06.pdf) | 回路図5枚: 1 USB_I/O（USB Type-C、VBUS保護、3.3 V LDO）、2 MCU（STM32、BOOT/RESET、LED、RTC電池）、3 CLK（24 MHz TCXOとcomparator）、4 I/O_Buffer（入力整形、出力buffer）、5 CONN_TP（GHコネクタ、テストパッド） |
| [`pcb_2026-09-06.pdf`](./pcb_2026-09-06.pdf) | PCB図8ページ: p1 表面の部品配置、p3～p4 BOM表、p5 表面、p8 裏面、ほかは層ごとの図 |
| [`bom_2026-09-06.csv`](./bom_2026-09-06.csv) | 部品表（EasyEDAのBOM出力をCSVにしたもの。U1だけ下の注意のとおり直してある） |

## 読むときの注意

- **U1（MCU）の品番:** 回路図PDFとPCB図のBOM表では`STM32G0B1RBT6` / C5143679になっているが、実際に実装したのは`STM32G0B1RCT6` / C3034735（Flashが256 KBの品。ピン配置は同じ）。発注時に品番を変えたが、EasyEDAの部品情報が古いまま出力された。CSVはRCT6に直してある。
- **基板の大きさと層数:** 60 × 38 mm（角R3 mm）、4層（Top、Inner1=GND全面、Inner2=信号とGND、Bottom=GND）。当初の設計値（75 × 50 mm、2層）とは違う（[`plan/design.md`](../../plan/design.md) §6.7、§7）。
- 部品はすべて表面にある。テストパッドTP1～TP9も表面にある。取付穴はない。
- USB Type-C（J1）は右の短辺、GHコネクタJ2～J8は下の長辺に左から順に並ぶ。
- F1（PTCヒューズ）は実装していない（BOMにない）。代わりにR3（0 Ohm）でVBUSをつないでいる。D1（TVS）は実装している。
- 受動部品はおもに0402である。
- 回路図のnet名は、設計文書と一部違う（[`hardware/overall/pin_assignment.md`](../overall/pin_assignment.md)の冒頭）。
