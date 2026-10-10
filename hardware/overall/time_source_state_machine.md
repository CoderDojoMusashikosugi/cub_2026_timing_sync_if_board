# 時刻源・PPS/NMEA状態遷移

## 基本方針

- 優先順位はGNSS、Jetson、STM32内蔵RTCの順とする。
- 外部へ出すPPSは常にU2 C22434890の24 MHz TCXOをU14でCMOS化したclockで駆動されるMCU timerから生成し、入力PPSやRTC出力を直接切り替えない。
- UTCが一度も確立していない状態では、全出力bufferをHigh-Zにする。
- GNSS喪失後もPPS/NMEAを継続するが、RMC statusは`V`とし、測位有効性を偽らない。
- 出力中の時刻補正は原則slewとし、短パルス、二重パルス、パルス欠落、秒の逆行を発生させない。
- 出力bufferのenable・High-Zの切替は、PPSがLowでNMEAを送っていない時点（firmwareでは秒内150 ms）だけで行い、PPSのパルスやNMEAの文を途中で切らない。High-Zは秒境界を動かすstepより前に完了させる。clock異常によるresetだけは例外で、OE_Nのpull-upで直ちにHigh-Zになる。

## Clock・内蔵RTC設定

- U2 YXC `OW7EL89CENUYO3YLC-24M` / C22434890のclipped-sine出力をC11でAC結合し、1.031 V biasしたU14 `LMV7219M7X/NOPB` / C544337で3.3 V CMOS化する。電源投入後5 ms以上待ち、R8経由でHSE bypassへ入力する。PLLM=2、PLLN=16、PLLR=4でSYSCLK/TIM2=48 MHz、PLLQ=4でUSB=48 MHzとする。起動時はVoltage Range 1へ設定して`VOSF=0`を確認し、Flash latencyを2 wait stateへ上げてからPLL/SYSCLKを切り替える。HSE CSSを有効化する。C48677994/C51822852/C51966238は使用しない。
- HSE bypass入力はPF0で24 MHz、3.3 V CMOS、duty 45～55 %を満たすことを試作合否条件とし、条件外の基板は不合格とする。
- X1 32.768 kHz LSEをSTM32内蔵RTCのclock源とし、CR2032からVBAT domainを保持する。SC-32SのESR最大70 kOhm、C0 typ 1 pF、CL 7 pFから求めた`gmcrit`概算0.76 uA/Vに対し、`LSEDRV=10`（medium-high、`Gmcritmax` 1.7 uA/V）を使用する。LSEはcalendar/backup専用で、powered holdoverのPPS周波数基準には使わない。
- STM32G0B1 errata ES0548 §2.2.1の回避として`LSECSSON=0`とし、LSE CSSは使用しない。LSE CSSはVBAT mode中に動作しないため、主電源断中のLSE継続を保証する判定にも使用しない。
- 使用する全backup registerに固定signature、version、最後に検証済みのTCXO補正値、同期時刻、application payload CRCを保存する。更新はCRCを最後に書く二段階手順とし、中途半端な書込みを有効扱いしない。これとは別に、`RCC_BDCR`の設定bitとRTC設定registerを対象としたRTC-domain configuration CRCを保存する。LSERDY、calendar、SSRなどhardwareが更新するstatus/data bitはCRC対象から除外する。
- STM32G0B1 errata ES0548 §2.2.11への対策として、起動処理の最初かつreset flagをclearする前に`RCC_CSR.BORRSTF`を保存し、signature/version、両CRC、`RTC_ICSR.INITS`、`RCC_BDCR`のRTCSEL/LSEON/LSERDY/RTCEN、calendar範囲、単調なSSRを検証する。CRC不一致または設定不整合があれば`RCC_BDCR.BDRST`をset、read-back、clearしてbackup domainを初期化し、RTC時刻を無効とする。正常時はBDRSTを発行せず保持時刻を使い、検証後にreset flagをclearする。
- CR2032がVBATを保持するため、ES0548 §2.2.11の第1workaround（VDD/VBATを100 mV以下へ200 ms超低下）は通常運用で実行できない。上記のsoftware erase/CRC検証を必須とし、省略しない。
- RTC calendarを更新してINIT modeを抜けた後、再度INITへ入る場合は`BYPSHAD`をclearして`RSF`の立上りを待つ。calendar全registerは1回のINIT sessionで書き、ES0548 §2.9.1の連続INIT不具合を回避する。
- `RTC_VALID`は起動時に読み出せたRTC-domainの整合性を示すだけで、VBAT期間中にLSEが停止しなかったことや電池残量を証明しない。
- RTC-only起動ではRTC calendarとsubsecond register（SSR）を連続して整合読出しし、1 Hz wakeup eventを補助にTIM2の秒内位相を初期化する。2回連続した単調更新を確認してから出力をenableする。
- GNSS有効中はPPSからTCXOの実偏差を連続推定し、ARR補正値を更新する。holdover開始時は最後に検証済みの補正値をそのまま引き継ぐ。
- 一度もGNSS同期していない新品基板ではTCXO agingを補正できないため、RTC-only起動で得たPPSはR-013保証対象外とする。少なくとも1回GNSS同期し、補正値をCRC付きbackup registerへ保存した後にholdover精度を保証する。
- R-013（GNSS消失後5分で1 ms以下）は主電源ON、TCXO/TIM2継続動作中のholdover要求である。主電源OFF中の時刻精度はLSEの±20 ppm級であり、同じサブms要求を適用しない。
- HSE/PLL/CSS fault時はHSIへ診断退避するが、時刻品質を保証できないため全出力をHigh-Zにし、`FAULT_CLOCK`へ遷移する。

## 状態

| State | 進入条件 | PPS/NMEA出力 | 遷移 |
|---|---|---|---|
| `INIT_INVALID` | power-on、UTC未確立、RTC無効、LSE不起動 | 全channel High-Z。ただしTCXO/TIM2と診断通信は動作可 | GNSS確立→`GNSS_LOCKED`、有効Jetson SET→`JETSON_SET`、RTC有効→`TCXO_HOLDOVER` |
| `GNSS_ACQUIRE` | PPSまたはNMEAの一方だけ有効 | 既存の有効時刻があればholdover出力、なければHigh-Z | PPS+valid NMEAを5回連続→`GNSS_LOCKED`、timeout→前状態 |
| `GNSS_LOCKED` | PPS周期正常、checksum/UTC/status=`A`のNMEAを5回連続 | 規律済みPPS、RMC=`A`。VLPだけUTC+1秒・checksum再計算 | PPS 2回連続欠落、NMEA timeout/invalid→`TCXO_HOLDOVER`、大きな位相差→`FAULT`、clock異常→`FAULT_CLOCK` |
| `JETSON_SET` | GNSS無効中にCRC・長さ・範囲検証済みSETを受信 | 下記step/slew規則に従う | 設定完了→`TCXO_HOLDOVER`、GNSS確立→`GNSS_LOCKED` |
| `TCXO_HOLDOVER` | GNSS無効、内蔵RTC有効、または有効時刻を保持中 | TCXO/TIM2 PPSを継続、全NMEA RMC=`V`。VLPはUTC+1秒処理を維持 | GNSS 5回連続正常→`RESYNC`、有効Jetson SET→`JETSON_SET`、RTC/MCU時刻異常→`FAULT`、clock異常→`FAULT_CLOCK` |
| `RESYNC` | holdoverからGNSS復帰 | PPS/NMEA継続。周期補正は±100 us/s以内 | 位相差収束→`GNSS_LOCKED`、GNSS再喪失→`TCXO_HOLDOVER` |
| `FAULT` | UTC矛盾、timer異常、補正不能な位相差 | 全出力High-Z、状態通知 | 明示的な再初期化または有効時刻SET |
| `FAULT_CLOCK` | HSE CSS、PLL unlock、TCXO/clock異常 | 全出力High-Z。HSIは診断通信だけに使用 | clock再初期化成功後にUTC/RTCを再検証。自動でPPSを再enableしない |

LSE不起動は`FAULT_CLOCK`ではなく`RTC_INVALID`として`INIT_INVALID`へ入れる。TCXO/TIM2によるPPS/NMEA生成能力は残るため、GNSSまたはJetsonでUTCが確立すれば動作できるが、Jetson不在時のRTC-only起動要求R-012は満たさない。

## Jetson SETの適用

frame、CRC、sequence、応答、timeoutは[`jetson_time_protocol.md`](./jetson_time_protocol.md)を正とし、受信frameのCRCと範囲検証に合格したSETだけを以下へ渡す。

1. `INIT_INVALID`では、SET値を次の整数秒境界でMCU/RTCへstep適用する。適用完了まで出力はHigh-Zとする。
2. 有効時刻の動作中でoffset絶対値が100 ms以下なら、PPS周期補正を±100 us/s以内に制限してslewする。UTCを逆行させない。
3. offset絶対値が100 msを超えるSETは通常commandでは拒否し、現在値・要求値・offsetを応答する。
4. 明示的な`FORCE_SET`だけは、全出力をHigh-Zにしてから次の整数秒境界でstepし、2秒連続の正常更新後に再enableする。
5. GNSS有効中のSET/FORCE_SETは拒否し、RTCも変更しない。

## Holdover出力の扱い

- Jetson、Spresense、AUX、VLP-32C、Mid-360の全channelへPPSとRMC=`V`を継続する。
- VLP-32C資料はRMC validityを確認し、`V`の場合はGPS positionを無効として扱うよう求めている。Mid-360資料はGPRMC/GNRMCを要求するが、status=`V`で同期を維持する保証を記載していない。
- したがってholdover中のVLP-32C/Mid-360内部時刻同期は保証対象外とし、点群timestampと本基板PPSの追従を実機統合試験で確認する。`A`を偽装して送信してはならない。

## 文書段階の試験項目

- 有効backup、signature/application CRC異常、RTC-domain configuration CRC異常、`INITS=0`、LSERDY失敗、電池なしでの起動状態を確認する。異常時は必要なBDRST sequenceを実行して時刻無効、出力High-Zとなること。
- LSE不起動時に`FAULT_CLOCK`へ入らず`RTC_INVALID`となり、GNSS/Jetson取得後はTCXO/TIM2出力が可能であること。
- RTC INIT modeへ短時間に連続して入る試験を行い、`BYPSHAD` clear、`RSF`待ち、単一session書込みでcalendarが破損しないこと。
- U2 pin 3の振幅、U14入力common-modeとPF0の24 MHz、duty 45～55 %、jitterを0～40 ℃で確認し、Voltage Range 1/Flash 2 wait stateの順序を含むclock初期化試験を行う。
- HSE停止、PLL unlock、TCXO enable異常で`FAULT_CLOCK`へ入り、PPS/NMEAがHigh-Zになること。
- GNSS PPSのみ、NMEAのみ、checksum異常、RMC=`V`、両方喪失時の遷移。
- GNSS喪失5分後のPPS誤差1 ms以下。
- GNSS復帰時に短パルス、二重パルス、欠落、UTC逆行がないこと。
- Jetson SETの初期step、小offset slew、大offset拒否、FORCE_SET時High-Z。
- VLPの+1秒、日付繰上げ、checksum再計算がholdover中も成立すること。
- VLP-32C/Mid-360がRMC=`V`を受信した際のtimestamp動作。
