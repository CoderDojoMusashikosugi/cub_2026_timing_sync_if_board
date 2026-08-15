# 電源budget

- 対象: 初回試作、USB 2.0 bus-powered、外部機器への給電なし
- 合否基準: 通常最大動作時のVBUS電流を100 mA未満とする
- 注意: VBUS配線・R36・F1の0.5 Aは物理設計上限であり、通常消費の許容値ではない

## 3.3 V rail

以下は回路図release前の設計配分であり、部品datasheetの最大値と実測値で更新する。U2/U14はDR-050、R48/R49のbias値はDR-053で確定した。

| 負荷 | 設計配分 | 条件 |
|---|---:|---|
| U1 STM32G0B1 | 30.0 mA | 48 MHz、USB/UART/TIM2動作を含む暫定上限 |
| U4 74HC14 | 2.0 mA | 5入力toggle、U4F固定を含む |
| U7/U8/U12内部dynamic | 3.0 mA | 74LVC125A x3 |
| U7/U8/U12外部負荷 | 24.0 mA | 12出力すべて2 mA負荷の保守条件 |
| U2 YXC C22434890 | 2.0 mA | 3.3 V、24 MHz clipped sine、YSO510TP familyの15.1～26 MHz最大値 |
| U14 TI C544337 | 2.4 mA | LMV7219、datasheet最大値。出力負荷のdynamic分は試作確認 |
| R48/R49 bias | 0.069 mA | 3.3 V / 48 kOhm。R50の定常差動電流は無視可能 |
| U11 USB ESD leakage | 0.1 mA | budget上の丸め値 |
| D2 status LED | 1.5 mA | R24=1 kOhm |
| pull-up、bias、その他 | 1.0 mA | 同時worst-caseの丸め値 |
| **合計** | **66.1 mA** | **100 mAに対して33.9 mA余裕** |

試作ではU2を含む上表の各値を実測し、合計が100 mA以上ならUSB descriptorや500 mA物理経路を根拠に受け入れず、負荷削減またはself-powered化を再設計する。

## VBUS換算とU3損失

LDO構成なのでVBUS電流は概ね3.3 V rail電流と等しく、U3の自己消費を加えても設計値は約67 mAである。

| 条件 | U3損失 |
|---|---:|
| VBUS=5.00 V、I=66.1 mA | `(5.00 - 3.30) x 0.0661 = 0.112 W` |
| VBUS=5.25 V、I=66.1 mA | `(5.25 - 3.30) x 0.0661 = 0.129 W` |
| 設計上限I=100 mA、VBUS=5.25 V | `0.195 W` |

XC6206P332MR-Gの採用packageに対する`theta_JA`と許容junction温度はメーカー資料を回路図release前に確認する。初回試作では0～40 ℃、全出力負荷、USB通信連続、LED点灯でVBUS電流、3.3 V droop、U3表面温度を測定し、確認した`theta_JA`でjunction温度を再見積りする。

## TCXO rail

U2 C22434890とU14 C544337は`3V3`から給電する。C19 AC結合、R48/R49/C20の1.031 V reference、R50 input bias、C21 decouplingを使用し、`+2V8_CLK`とU15は使用しない。旧C48677994/C51822852/C51966238の電流値は流用しない。

## VBATとCR2032

| 項目 | budget |
|---|---:|
| STM32内蔵RTC + LSE + backup domain | 2.0 uA以下（部品選定・firmware設計配分） |
| CR2032公称容量の計算用仮定 | 200 mAh |
| 理想保持時間 | `200 mAh / 0.002 mA = 100000 h`、約11.4年 |
| 50 % derating後の目安 | 約5.7年 |

これは寿命保証値ではない。STM32 datasheetの該当LSE drive/VBAT条件で最大電流を確認し、電池の自己放電、温度、holder接触、R47=100 Ohmを含めて再計算する。主電源OFF時の実測VBAT電流が2.0 uAを超える基板は不合格とする。

## 試作記録

| 条件 | VBUS電流 | 3.3 V | U3表面温度 | VBAT電流 | 判定 |
|---|---:|---:|---:|---:|---|
| 0 ℃、最大動作 | TBD | TBD | TBD | TBD | TBD |
| 25 ℃、最大動作 | TBD | TBD | TBD | TBD | TBD |
| 40 ℃、最大動作 | TBD | TBD | TBD | TBD | TBD |
