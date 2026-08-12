# Firmware必須要件

本書は回路成立に必要なfirmware条件の索引である。状態遷移の詳細は[`time_source_state_machine.md`](./time_source_state_machine.md)、pin/register初期化の詳細は[`pin_assignment.md`](./pin_assignment.md)、Jetson frameは[`jetson_time_protocol.md`](./jetson_time_protocol.md)を正とする。

## 起動順序

1. 全`OE_N`を外付けpull-upとGPIO出力HighでHigh-Zに保つ。
2. reset flagをclearする前に`BORRSTF`を保存し、RTC-domain signature/version、両CRC、`INITS`、`RCC_BDCR`、calendar、SSRを検証する。
3. 不整合ならBDRSTをset、read-back、clearし、`RTC_INVALID`とする。CR2032使用時はsoftware erase/CRC手順を省略しない。
4. `PVM_VDDIO2=110`、UCPD dead-battery strobe、VREFBUF無効を設定する。
5. U2/U14の安定化に電源投入後5 ms以上待ち、Voltage Range 1、`VOSF=0`、Flash 2 wait state、HSE bypass/PLL、HSE CSSの順にclockを設定する。
6. PLLQ=4の48 MHzをUSB clockへ選択する。HSI48/CRSは使用しない。
7. TIM2、UART、DPLL、RTCを初期化し、有効時刻とclockを検証してから必要channelだけenableする。

## Clock・USB

- HSE=24 MHz、PLLM=2、PLLN=16、PLLR=4、SYSCLK/TIM2=48 MHz
- TIM2はARR=47999999、ARPE有効、CCR2=4800000。周波数・位相steeringはARRだけで行う
- GNSS PPSはU4Aで反転するためCH1 falling-edge capture
- PLLQ=4でUSB 48 MHzを生成する。HSI48+CRSへの依存を削除する
- HSE/PLL/CSS faultは全出力High-Z、`FAULT_CLOCK`。HSIは診断専用

## RTC・backup domain

- LSEDRV=10、LSECSSON=0。PC13へTAMP/RTC_OUTを割り当てない
- INIT modeを抜けた後に再度INITへ入る場合、`BYPSHAD`をclearし`RSF`立上りを待つ。calendar全registerを1回のINIT sessionで書く
- backup payloadはsignature、version、TCXO補正値、同期時刻、application CRCを持ち、CRCを最後にcommitする
- RTC-domain configuration CRCはsoftware設定bitだけを対象とし、hardware更新bitを含めない
- LSE不起動は`RTC_INVALID`であり`FAULT_CLOCK`ではない
- 一度もGNSS同期していないTCXO補正値でR-013を保証しない

## DPLL・出力

- GNSS PPS/NMEAを5回連続検証してlockする
- GNSS喪失時は最後に検証済みのARR補正を保持し、RMC=`V`で出力する
- 復帰時の補正は±100 us/s以内とし、短パルス、二重パルス、欠落、UTC逆行を発生させない
- GNSS追従中の出力PPS位相差は校正後1 us以内、holdoverは0～40 ℃・5分で1 ms以内

## Bring-up合否

- option byte変更前にSWD導通と`REV_ID`を記録し、安定給電でreload完了まで電源を維持する
- 0～40 ℃でU2出力振幅、U14入力common-mode、PF0 24 MHz、duty 45～55 %、jitter、USB列挙を確認する
- LSEは`Sf >= 3`、`DL < 0.5 uW`を0 ℃、40 ℃、CR2032単独給電で確認する
- fault injection、CRC破損、RTC連続INIT、Jetson frame破損、GNSS喪失/復帰を自動試験へ含める
