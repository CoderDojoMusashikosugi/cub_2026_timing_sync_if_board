# 現行ハードウェア設計

本ディレクトリは、回路図・PCB・firmware・試作ハーネスへ直接反映する現行情報だけを格納する。

- [`pin_assignment.md`](./pin_assignment.md): MCU、RTC、buffer、connector、電源の接続正本
- [`time_source_state_machine.md`](./time_source_state_machine.md): GNSS/Jetson/RTC、PPS/NMEA、faultの動作正本
- [`firmware_requirements.md`](./firmware_requirements.md): 起動順序、clock、RTC、DPLL、firmware合否条件
- [`jetson_time_protocol.md`](./jetson_time_protocol.md): Jetson SET/GETのCRC付きframe仕様
- [`power_budget.md`](./power_budget.md): 3.3 V、TCXO、VBATの電流・損失・合否基準
- [`bom_preliminary.csv`](./bom_preliminary.csv): 初回試作BOM（設計上の注記付き）。発注版のEasyEDA出力の部品表・回路図・PCB図は[`../rev1/`](../rev1/)
- [`pcb_layout_guidelines.md`](./pcb_layout_guidelines.md): レイアウト前に作った配置・配線ルールと図（[`figures/`](./figures/)）。外形・層数・test padの面などが発注版と違う。公開リポジトリには含めていない
- [`harness_pinout.md`](./harness_pinout.md): 基板側pinと試作ハーネス製造情報
- [`open_issues.md`](./open_issues.md): 未解決事項と設計・製造release条件

上位の要求とシステム構成は[`../../plan/design.md`](../../plan/design.md)を参照する。選定理由や過去案は[`../reviews/`](../reviews/)にあり、製造入力には使用しない。
