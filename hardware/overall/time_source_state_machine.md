# 時刻源・PPS/NMEA状態遷移

## 基本方針

- 優先順位はGNSS、Jetson、RTCの順とする。
- 外部へ出すPPSは常にMCU timerから生成し、入力PPSやRTC SQWを直接切り替えない。
- UTCが一度も確立していない状態では、全出力bufferをHigh-Zにする。
- GNSS喪失後もPPS/NMEAを継続するが、RMC statusは`V`とし、測位有効性を偽らない。
- 出力中の時刻補正は原則slewとし、短パルス、二重パルス、パルス欠落、秒の逆行を発生させない。

## RTC設定

- U2 `INT/SQW`は`INTCN=0`、`RS2:RS1=00`として1 Hz square waveに設定する。
- `BBSQW=0`とし、本基板主電源OFF中は`INT/SQW`をHigh-Zにする。CR2032はcalendar/TCXO保持だけに使用する。
- U2 `32kHz`からMCU PB10への回路接続は残す。初回firmwareで常時計測を有効にするかは[`open_issues.md`](./open_issues.md)のOI-002で決定する。
- 起動時にOscillator Stop Flag（OSF）を確認する。OSF=1、RTC読出し異常、日時範囲外、または電池交換後未設定ならRTC時刻は無効とする。
- RTC-only起動では1 Hz `INT/SQW` edgeをローカル秒境界としてMCU timer位相を初期化し、edge直後にcalendarを読み出してUTC秒を対応付ける。出力は2回連続して単調な秒更新を確認した後にenableする。
- GNSS有効中はまず`INT/SQW` 1 Hz edgeとMCU timerのoffsetを校正し、holdover開始時に最後の有効な補正値を引き継ぐ。32 kHz補正を有効化した場合は、GNSS PPSに対する32 kHzの周波数offsetも継続推定する。

## 状態

| State | 進入条件 | PPS/NMEA出力 | 遷移 |
|---|---|---|---|
| `INIT_INVALID` | power-on、UTC未確立、RTC無効 | 全channel High-Z | GNSS確立→`GNSS_LOCKED`、有効Jetson SET→`JETSON_SET`、RTC有効→`RTC_HOLDOVER` |
| `GNSS_ACQUIRE` | PPSまたはNMEAの一方だけ有効 | 既存の有効時刻があればholdover出力、なければHigh-Z | PPS+valid NMEAを5回連続→`GNSS_LOCKED`、timeout→前状態 |
| `GNSS_LOCKED` | PPS周期正常、checksum/UTC/status=`A`のNMEAを5回連続 | 規律済みPPS、RMC=`A`。VLPだけUTC+1秒・checksum再計算 | PPS 2回連続欠落、NMEA timeout/invalid→`RTC_HOLDOVER`、大きな位相差→`FAULT` |
| `JETSON_SET` | GNSS無効中に認証済みSETを受信 | 下記step/slew規則に従う | 設定完了→`RTC_HOLDOVER`、GNSS確立→`GNSS_LOCKED` |
| `RTC_HOLDOVER` | GNSS無効、RTC有効、または有効時刻を保持中 | MCU timer PPSを継続、全NMEA RMC=`V`。VLPはUTC+1秒処理を維持 | GNSS 5回連続正常→`RESYNC`、有効Jetson SET→`JETSON_SET`、RTC/MCU時刻異常→`FAULT` |
| `RESYNC` | holdoverからGNSS復帰 | PPS/NMEA継続。周期補正は±100 us/s以内 | 位相差収束→`GNSS_LOCKED`、GNSS再喪失→`RTC_HOLDOVER` |
| `FAULT` | UTC矛盾、timer異常、補正不能な位相差 | 全出力High-Z、状態通知 | 明示的な再初期化または有効時刻SET |

## Jetson SETの適用

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

- RTC正常/OSF=1/電池なしでの起動状態と出力High-Z。
- GNSS PPSのみ、NMEAのみ、checksum異常、RMC=`V`、両方喪失時の遷移。
- GNSS喪失5分後のPPS誤差1 ms以下。
- GNSS復帰時に短パルス、二重パルス、欠落、UTC逆行がないこと。
- Jetson SETの初期step、小offset slew、大offset拒否、FORCE_SET時High-Z。
- VLPの+1秒、日付繰上げ、checksum再計算がholdover中も成立すること。
- VLP-32C/Mid-360がRMC=`V`を受信した際のtimestamp動作。
