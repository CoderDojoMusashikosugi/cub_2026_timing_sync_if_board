# Firmware試験計画

- 対象: timing_sync_if 初回試作基板（U1 `STM32G0B1RCT6`）と、その上で動作するfirmware
- 作成日: 2026-10-03
- 状態: 初版（2026-10-03）の計画に、決まった暫定値（§2.1）を足したもの。firmwareはbring-up、selftest、製品firmware（app）の3 imageを実装済みで、2026-10-03、10-04、10-10に2枚の基板で試験した。試験の結果は基板ごとの記録にある（公開リポジトリには含めていない）
- 計画と実装の違い（2026-10-17時点）:
  - L1 host試験は`ts_crc32c`、`ts_dpll`、`ts_jetson_proto`、`ts_nmea`、`ts_time`の5 moduleだけである。状態機械とbackup-domain検証はtarget専用のsource（`timesync.c`、`rtc_keep.c`）にあり、hostでbuildできないので、UT-09とUT-11はhost試験になっていない。UT-12（静的解析）の設定もない。selftestの`ut`（UT-13）も同じ5 moduleだけを実行する
  - build種別はdebug/test/releaseではなく、bringup/selftest/appの3 imageである。全出力のon/offとTXごとのenable（`out`）はappにもある
  - T-04の注入機能（bit反転、byte欠落・追加、切断、timeout、CRC不一致）は`jetson_time.py`にまだない。`jetson_time.py`は`status`、`set`、`force-set`、`watch`だけである
  - 未実装: watchdog（P-14）、Jetsonの有効期限（P-13）、大offset拒否時の現在値などの応答（P-12）、frame errorの連続回数の記録、基板ごとのPPS offsetの保存（P-06）、debug buildとTP9 trace、J7/J8のRXの利用
- 位置づけ: 試験の**方法・順序・合否条件の出典**をまとめた計画書であり、仕様の正本ではない。合否の数値は下記の正本から引用する。正本で数値が決まっていない項目は§2のTBDとして扱い、本書で勝手に値を決めない

参照する正本:

| 内容 | 文書 |
|---|---|
| 要求R-001～R-018、検証・合否条件 | [`../plan/design.md`](../plan/design.md) §2・§8 |
| pin、register初期化、bring-up手順、TIM2構成 | [`../hardware/overall/pin_assignment.md`](../hardware/overall/pin_assignment.md) |
| 状態遷移、RTC/backup domain、Jetson SET適用規則 | [`../hardware/overall/time_source_state_machine.md`](../hardware/overall/time_source_state_machine.md) |
| firmware必須要件、bring-up合否 | [`../hardware/overall/firmware_requirements.md`](../hardware/overall/firmware_requirements.md) |
| Jetson frame仕様と合否試験 | [`../hardware/overall/jetson_time_protocol.md`](../hardware/overall/jetson_time_protocol.md) |
| 電流・VBATの合否と記録表 | [`../hardware/overall/power_budget.md`](../hardware/overall/power_budget.md) |
| 試作合否試験、未解決事項 | [`../hardware/overall/open_issues.md`](../hardware/overall/open_issues.md) |

## 1. 試験方針

### 1.1 試験レベル

| Level | 名称 | 実行環境 | 主な対象 | 実行タイミング |
|---|---|---|---|---|
| L1 | host単体試験 | PC（gcc/clang）、HALはmock | NMEA解析・生成、VLP +1秒、calendar変換、CRC32C、Jetson frame、状態機械、DPLL制御則、backup-domain検証ロジック | commitごと |
| L2 | 実機HIL試験 | 試作基板 + PC + GNSSエミュレータ + USB-UART | 周辺設定、出力、PPS/DPLL、RTC、状態遷移、protocol、fault注入 | firmware変更ごとにsmoke、release前に全項目 |
| L3 | 実機統合試験 | 試作基板 + UM982 + Jetson/VLP-32C/Mid-360/Spresense level board | 実機器での受信、同期、timestamp | P6以降、release前 |
| L4 | 環境・長時間試験 | 恒温槽0/25/40 ℃、24～72 h連続 | holdover精度、clock、LSE、USB、電流、異常が起きないこと | release前 |

L1でロジックを網羅し、L2以降は「実機でしか確認できないこと」（register設定、edge極性、波形、timing、電源・温度）に集中する。

### 1.2 試験しやすくするためのfirmware設計ルール

実装の最初から次を守る。後付けではL1/L2が成立しない。

1. 時刻、NMEA、CRC、frame、状態機械、DPLL、backup検証のロジックはHAL/register非依存のCモジュールとし、hostでもbuildできるようにする。register操作は薄いdriver層へ分ける。
2. 時刻（tick）を外から与えられるようにし、timeout判定（inter-byte 20 ms、NMEA timeout、`valid_for_ms`）をhostで再現できるようにする。
3. build ID（git hash、build種別 debug/test/release）をUSB CDCで取得できるようにする。
4. 試験用hook（§3.3）はtest buildだけで有効にし、release buildではcompile時に除外する。製品機能はhookに依存させない。
5. `TIME_STATUS` 26 bytesなどのwire formatは`static_assert`で固定する。
6. compiler警告ゼロ（`-Wall -Wextra -Werror`）、静的解析（cppcheckまたはclang-tidy）の指摘ゼロ、または理由付きの除外とする。

### 1.3 実行頻度

- commitごと: L1全項目、debug/test/release 3種のbuild成功
- firmware変更ごと: L2 smoke。BU-06の起動確認、CK-01、IO-01の電源投入1回、UA-02、PP-02、JP-01、SM-03の短縮版（目安10分）
- release候補: L1～L4全項目

## 2. 試験前に決める必要がある未定義事項

以下は正本に数値・挙動の定めがなく、決まるまで該当試験の合否を判定できない。決定したら[`../hardware/reviews/decision_log.md`](../hardware/reviews/decision_log.md)へDRとして記録し、正本へ反映してから本書の合否欄を更新する。**P-03/P-05/P-11/P-12/P-15はfirmware構造に影響するため実装開始前に決める。**

| ID | 未定義事項 | 正本の現状・不整合 | 影響する試験 | 判断材料 |
|---|---|---|---|---|
| P-01 | GNSS PPS「周期正常」の許容幅と、glitch（短パルス・余分なedge）の除外条件 | 状態遷移表に「PPS周期正常」とだけある | PP-07、PP-08、SM-03 | UM982 PPSのjitter、TCXO偏差、DPLL capture range |
| P-02 | `GNSS_ACQUIRE`のtimeout秒数 | 「timeout→前状態」とだけある | SM-02 | |
| P-03 | `FAULT`へ入る位相差の閾値（「大きな位相差」「補正不能な位相差」） | 数値なし | SM-05、RT-08 | slew上限±100 us/sでは位相差Xの回復にX÷100 us秒かかる（10 ms→100秒、1秒→約2.8時間） |
| P-04 | `RESYNC`→`GNSS_LOCKED`の収束判定（位相差と継続秒数） | 「位相差収束」とだけある | PP-11 | |
| P-05 | RTC-only起動の後、GNSSを初めて取得したときの扱い（slewで戻す、High-Zにしてstepする、`FAULT`にする、のいずれか） | 規定なし | RT-08 | LSE ±20 ppmでは電源断1日あたり最大約1.7秒ずれる。P-03と一緒に決める |
| P-06 | PPS入力経路の固定offset（基板ごと）の保存先、書込み手順、未校正時の既定値 | 「実測・校正しDPLL位相補正へ含める」とだけある | PP-04 | flashに置く場合、page番号がRB/RC/REで異なる（DR-060） |
| P-07 | J2（UM982）のNMEA入力baud、使用sentence（GNRMC/GPRMC/GGA）、UM982側の設定（`CONFIG PPS`の極性・幅、出力message） | OI-001の一部。UM982の既定値に依存 | UA-01、PP-09 | `CONFIG PPS ENABLE`はloss of lock後も約30秒PPSを出し続ける（Unicore Reference Commands Manual §4.32） |
| P-08 | 出力NMEAの構成: channelごとのsentence（RMCのみか、VLP向けにGGAも出すか）、talker（GP/GN）、baud | Q-009の初期値「GPRMC、9600 baud、8N1、1 Hz」だけ。VLP/Spresense/AUXの個別値なし | UA-02、UA-04 | VLP-32CはGPRMCまたはGPGGA、Mid-360はGPRMC/GNRMCを受け付ける |
| P-09 | holdover中・`RESYNC`中のRMC内容（position等のfieldを空欄にするか最終値か、`RESYNC`中のstatusはAかVか） | holdoverで`V`にすることだけ規定 | UA-04、SM-07 | |
| P-10 | channelごとのNMEA送信開始時刻 | VLPの時間窓（PPS立下りから50 ms以上後に開始、送信完了から次のPPS立上りまで300 ms以上）とMid-360の時間窓（立上り後0～430 ms推奨）だけある | UA-05 | 9600 baudでRMC 1文（約70 byte）の送信に約73 msかかる |
| P-11 | J3 TXでNMEAとJetson protocol応答を混在させるか | design §5はJetsonへNMEAを1系統出すとし、§3.4は同じUARTを制御portとする | UA-04、JP-01、SY-01 | 混在させる場合、Jetson側は`$`と`TS` magicで振り分ける |
| P-12 | Jetson protocolの未定義挙動: NACK・`last error`の数値enum、同じsequenceで異なるpayloadの扱い、大offset拒否時の応答内容、`FAULT`から抜ける「明示的な再初期化」のcommand | 状態遷移文書は大offset拒否時に「現在値・要求値・offsetを応答」とするが、protocolのNACK payloadは2 bytesしかない。再初期化commandは未定義 | JP-04、JP-08、SM-05 | NACK codeと`last error`の数値は[`core/include/ts_jetson_proto.h`](./core/include/ts_jetson_proto.h)で定義済み（protocolが「実装enum」としているため）。残りは未決定 |
| P-13 | Jetsonを継続時刻源とする場合の有効期限と、期限切れ後の遷移 | design §4.4に記載があるが、状態遷移表は「設定完了→`TCXO_HOLDOVER`」だけ | SM-06 | |
| P-14 | watchdog（IWDG）の周期と、watchdog/BOR/SW1 reset後に出力を再enableする条件 | design §6.1に「ウォッチドッグ」とあるだけ | IO-01、LR-05 | reset中は外付けpull-upでHigh-Z。再起動後はTIM2位相が失われ、RTC-only起動と同じ扱いになる |
| P-15 | USB CDC command仕様（状態取得、ログ、全出力High-Z、bootloader移行、試験hook） | design §4.5に用途だけある | BU-09、PW-05、§3.3全般 | |
| P-16 | status LEDの状態別表示 | 規定なし | IO-08 | |
| P-17 | RTCの年範囲（2000～2099）とJetson SETで受け付けるUnix秒の範囲、入力NMEAのうるう秒（`23:59:60`）の扱い | 規定なし | UT-03、UT-05、UA-06 | |
| P-18 | holdover精度試験の前にGNSSでlockしておく時間 | 「少なくとも1回GNSS同期し補正値を保存した後」とだけある | PP-10 | |
| P-19 | firmware容量の上限（RB 128 KBへ戻せる余地を残すか） | DR-060でRB/RC/RE間の移行を許容している | BU-11 | RCはdual bank（128 KB×2、option byte `DUAL_BANK=1`）。ES0548 §2.2.10によりprefetch有効時はbankをまたぐ分岐が失敗し得るため、codeをbank 1（128 KB）以内に収めるか`PRFTEN=0`のままにする（bring-up firmwareは`PRFTEN=0`、約21 KB） |
| P-20 | HSE（TCXO系）異常時にUSBをHSI48+CRSで動かしてよいか | 正本は「HSI48/CRSは使用しない」「HSIは診断専用」。bring-up firmwareは、HSE異常時だけ診断用にUSB=HSI48+CRSとしてCDCを生かしている（時刻出力はHigh-Zのまま） | CK-05、BU-12 | 使わない場合、HSE異常時はUSBが使えずSWDでしか状態を読めない |
| P-21 | USBのVID/PIDと製品文字列 | 規定なし。bring-up firmwareはTinyUSBの試験用VID 0xCAFE / PID 0x4001を仮に使っている | BU-12、SY-01 | |
| P-22 | Rev A（`REV_ID=0x1000`）の基板でoption byteを変更するか（BU-07、SW2+SW1によるDFU） | 1枚目の基板はRev Aだった（2026-10-03）。ES0548 §2.2.9により、option byte変更が失敗するとBOOT_LOCKとRDP Level 1が設定され、debugも書換えもできなくなる | BU-07、BU-08 | 変更しない場合の復旧手段はUSBの`dfu` command、HardFault時の自動bootloader移行、SWD。firmwareが起動直後に止まった場合はSWDだけになる |

### 2.1 決定済みの項目と、firmwareの暫定値（2026-10-04）

DR-061で決めた項目:

- P-03: `FAULT`の閾値は位相差100 ms
- P-05: GNSS以外の時刻（RTC、Jetson）からGNSSへ移るとき、位相差1 ms以下ならslew、超えたらHigh-Zにしてstepし、2秒正常の後に再enable
- P-11: Jetsonとの双方向通信はUSB CDC（interface 2）。J3のUARTはNMEA送信だけ

製品firmware（`app` image、[`target/src/timesync.c`](./target/src/timesync.c)冒頭）に入れた暫定値。実機の結果を見て決め、DRへ記録する:

| ID | 暫定値 |
|---|---|
| P-01 | GNSS PPS間隔の許容は1秒±100 ppm。`GNSS_LOCKED`中は100 usより離れたPPSを欠落とみなす |
| P-04 | 位相差1 us以内が5秒続いたら`RESYNC`→`GNSS_LOCKED` |
| P-06 | PPS経路の固定offsetは全基板共通の6 count（125 ns、J3→J2折返しの実測）。基板ごとの保存はしない |
| P-07 | J2の受信baudは115200から自動選択（9600～921600）。RMCはGP/GNどちらも受ける。RMCは直前のGNSS PPS（990 ms以内）の時刻として扱う。受信は'$'からchecksumまで120文字まで受け付ける（UM982は緯度経度を分の小数8桁で出すため、測位後のRMCが規格の80文字を超えて83文字前後になる） |
| P-08 | 全channel GPRMC、9600 baud、8N1。時刻の小数部はVLP（J4）だけなし、他は2桁。緯度経度は分の小数5桁に切り捨て、82文字以内に収める |
| P-09 | holdover・`RESYNC`中はstatus `V`、mode `N`、位置fieldは空欄 |
| P-10 | 全channel、PPS立上りの200 ms後に送り始める。出力のenable・High-Zの切替はどちらも150 ms（PPSがLow、NMEA送信前）で行い、High-Zは最大約1.15秒遅れる。High-Zを決めた後はNMEAを送らない。enableした秒はその秒境界のPPSが出ていないので、J4（次のPPSの時刻）だけ送る |
| P-12 | `FAULT`からは`FORCE_SET_TIME`、`SET_TIME`、consoleの`reinit`で抜ける。同じsequenceで内容が違うrequestは新しいrequestとして扱う |
| P-15 | console（interface 0）に`status`、`gnss`、`out`、`reinit`を加えた |
| P-16 | LED: `GNSS_LOCKED`はPPSに合わせ100 ms、holdover系は2回点滅、`FAULT`は速い点滅、時刻なしは短く1回 |

時刻保持（内蔵RTC、[`target/src/rtc_keep.c`](./target/src/rtc_keep.c)）の実装上の決め事:

- backup registerは5本（TAMP_BKP0R～BKP4R）しかないので、signature/version/flags、検証済みTCXO補正値、最後の同期時刻、application CRC、configuration CRCだけを置く。**Jetsonの`last source_id`/`last sequence`は電源の入れ直しで0に戻る（JP-10の一部を満たさない）**
- RTCのcalendarは、GNSS lock時、Jetson SETの適用後、RTCの秒が生成PPSの秒と違うとき、RTCの秒境界が生成PPSから5 ms以上ずれたときに書き直す。INIT解除は生成PPSの秒境界でTIM2割込みから行う
- `TAMP_CR1`のITAMP4E（HSE monitoring）は起動ごとに0にする。初期値のままだと電源断のたびにHSE CSSがbackup registerを消す（2026-10-10、UID …000E0038で実測）
- smooth calibration（CALR）は、`GNSS_LOCKED`中にLSEの測定が300秒たまってから、LSEの偏差（TCXO基準の測定＋GNSSで求めたTCXO偏差）で決める
- LSEが動いていない（RTC_INVALID）ときのJetson SETは、RTCへ書かずに時刻だけ使い、ACKを返す
- Jetson SET（step）のACKは、時刻の適用とcalendarの書込み・読み返し・backup commitの後に返す（約7秒）

未実装: watchdog（P-14）

## 3. 試験環境

### 3.1 機材

| 機材 | 用途 | 区分 |
|---|---|---|
| PC（Python 3、pytest、pyserial、pyOCDまたはOpenOCD、STM32CubeProgrammer、dfu-util） | 自動試験、書込み、register読出し | 必須 |
| SWD probe（ST-LINK V2/V3）とTP1～TP5用のpogo/clip | 書込み、復旧、register監査 | 必須 |
| 3.3 V USB-UART ×4以上 | J2へのNMEA注入、J3 RX経路の受信試験（UA-01）、J7/J8、TX出力の受信 | 必須 |
| logic analyzer（8 ch以上、25 MS/s以上） | 6系統TXとPPSの時間関係 | 必須 |
| oscilloscope（4 ch、帯域200 MHz以上）、1 pF以下のactive probe | PF0 24 MHzのduty/jitter、出力波形、LSE振幅 | 必須（active probeはLSE試験だけ） |
| time interval counter、またはT-03 PPS timestamper | PPS間隔・位相差の長時間記録 | 必須 |
| UM982 module + antenna、UM982設定用USB-UART | 実GNSS源 | 必須 |
| 1.5 kOhm負荷、測定用ハーネス（GH→pin header）、loopbackハーネス | J4負荷試験、各出力の測定、J3/J7/J8折返し | 必須 |
| USB電流計またはDMM、4.75～5.25 Vに調整できるUSB電源 | VBUS電流、電源範囲 | 必須 |
| uA計またはSMU | 主電源OFF時のVBAT電流 | 必須 |
| USB電源の自動ON/OFF（relayまたはport電源制御付きhub） | power cycle試験の自動化 | 推奨 |
| 恒温槽（0～40 ℃） | EN-01、EN-02 | 必須（手配要確認） |
| Jetson Orin Nano Developer Kit、VLP-32C、Mid-360、Spresense level board | L3統合試験 | 必須（L3） |

### 3.2 自作する試験ツール

| ID | ツール | 内容 | 必要になる段階 |
|---|---|---|---|
| T-01 | GNSSエミュレータ | 小型MCU board（Nucleo、RP2040等）でPPS + NMEAを生成する。日時を任意に設定でき、PPS欠落・glitch・二重パルス、NMEAのchecksum異常・途中切れ・`V`・時刻飛び、周波数offset、位相stepを注入できる。実GNSSでは作れない日付境界と異常系を決まった手順で再現するために使う | P3 |
| T-02 | GNSSゲート治具 | UM982とJ2の間に3.3 V buffer（`74LVC1G125`等、UM982側の3.3 Vで給電）を入れ、PCからPPS/NMEAを個別に切断・復帰させる。gate前のPPSは真値としてT-03とscopeへ分岐する。**hot-plug禁止の運用条件を守ったままGNSS喪失を作るための治具**。切断中のJ2入力は基板のR14/R15 biasでPPS Low、NMEA idle Highになる | P5 |
| T-03 | PPS timestamper | 真値GNSS PPSと基板出力PPS（TP6または各コネクタpin 2）の全edgeを記録し、間隔・パルス幅・位相差を算出して、短パルス・二重パルス・欠落を自動判定する。RP2040 PIO（8 ns分解能）またはtime interval counterで実現する | P3 |
| T-04 | Jetson protocolクライアント | Python実装（[`tools/jetson_time.py`](./tools/jetson_time.py)）。USB CDC interface 2へつなぐ。`GET_STATUS`/`SET_TIME`/`FORCE_SET_TIME`と、bit反転・byte欠落/追加・切断・timeout・CRC不一致の注入。Jetson側実装の参照にもする | P3 |
| T-05 | NMEA受信・検証器 | 6系統のTXを同時に受信し、checksum、status、時刻の連続性、PPSとの時間関係を検証する。VLPは反転信号を受けられるadapterまたはlogic analyzerの反転decodeで読む | P3 |
| T-06 | HIL runner | pytestで基板CDC、T-01～T-05、USB電源制御をまとめて動かし、結果を§7の形式で保存する | P2から段階的に |
| T-07 | 治具類 | 測定用ハーネス、loopback（pin 3→pin 4）、中点bias（3V3を100 kOhm/100 kOhmで分圧、High-Z/Low/Highを見分ける）、1.5 kOhm負荷 | P2 |

### 3.3 試験用firmware hook

USB CDC経由で使う。command体系はP-15で決める。「製品」と書いたものはrelease buildにも残る製品機能である。

| ID | hook | 用途 | build |
|---|---|---|---|
| H-01 | 状態dump: state、flags、ARR、位相誤差、補正値、保存した`BORRSTF`等のreset要因、`REV_ID`、UID、build ID、UARTのORE/FE回数、frame error回数、stack使用量の最大値 | 全試験の観測 | 製品（項目は絞ってよい） |
| H-02 | 出力enableの制御: 全High-Z、個別enable | PW-05（電源OFF前の全High-Z）、IO-03 | 全High-Zは製品、個別enableはtest |
| H-03 | GNSS入力をsoftwareで無視する | L2でのholdover・復帰試験の簡易版 | test |
| H-04 | 観測専用capture: holdover中もCH1 captureを続け、GNSS PPSとの位相差を毎秒出力する（制御には使わない） | PP-10/PP-12/LR-02の自動記録 | test |
| H-05 | DPLL停止、ARR公称値で固定 | CK-04のTCXO自走偏差測定 | test |
| H-06 | 出力固定: PPS High固定、TX idle High固定 | PW-01の最大負荷電流、IO-04のDC電圧測定 | test |
| H-07 | fault模擬: CSS/PLL unlock処理経路、LSE不起動、RTC書込み失敗、backup register破壊（signature/CRC/`INITS`）、main loop停止（watchdog発動） | CK-05、RT-03、RT-06、JP-11、LR-05 | test |
| H-08 | LSE評価mode（LSEだけを`LSEDRV=10`で動かし、他の動作を止める） | RT-01 | test |
| H-09 | system bootloaderへの移行 | BU-09 | 製品 |
| H-10 | TP9 trace eventの選択 | ISR処理時間・処理順序の観測 | debug |

backup register破壊は、firmwareに依存しない方法としてSWDからの直接書込みでも行う（RT-03）。

### 3.4 試験系の構成

```text
UM982 + antenna ──> T-02 gate治具 ──PPS/NMEA──> J2
       │（gate前のPPS = 真値）
       └──────────────> T-03 timestamper / oscilloscope <── J3～J8 pin 2、TP6
T-01 GNSSエミュレータ ──（T-02の代わりにJ2へ接続）
PC ─ USB（CDC console、CDC Jetson protocol（T-04）、DFU、給電。USB電源制御経由）──> J1
PC ─ ST-LINK ──> TP1～TP5
PC ─ USB-UART ──> J3 pin 4（受信経路の試験、UA-01）
PC ─ USB-UART / logic analyzer（T-05）<── J3～J8 pin 3
```

### 3.5 試験時の運用制約

正本の運用条件（[`pin_assignment.md`](../hardware/overall/pin_assignment.md)「試作機の電源順序制約」）を試験中も守る。

1. J2～J8は全機器の電源OFF中に接続・切断する。**GNSS喪失はケーブル抜去で作らない。** T-02、UM982のcommand（`UNLOG`、`CONFIG PPS DISABLE`）、またはH-03で作る。
2. 本基板を入力源（UM982、T-01、USB-UART）より先、または同時に給電する。USB-UARTはUSB電源を切り替えられるhubへまとめ、基板の給電後にONする。
3. 接続先をOFFする前にH-02で全出力をHigh-Zにする。
4. SWD接続後にBOOT+RESETでDFUへ入る場合は、probeを外し、USBを含む全電源を切ってから操作する（ES0548 §2.2.8）。
5. USB-UARTは3.3 V品だけを使う。PA4/PA5は3.6 V耐圧（`TT`）である。

## 4. 試験段階とゲート

| 段階 | 内容 | 試験ID | 開始条件 | 終了条件 |
|---|---|---|---|---|
| P0 | 受入・初回通電（firmwareなし） | BU-01～BU-03 | 基板受領 | 全数で短絡なし、3V3正常、空flashでDFUとして見える |
| P1 | 書込み経路の確立 | BU-04～BU-11 | P0 | 全数でDFU/SWD/option byte/BOOT+RESETが成立し、`REV_ID`を記録済み |
| P2 | 基本機能（bring-up firmware） | BU-12、CK、IO、UA-01～UA-03、PP-01～PP-03、RT-01～RT-02 | P1、T-06/T-07の準備 | register監査が全項目合格、全channelの入出力を確認 |
| P3 | 時刻機能 | UA-04～UA-08、PP-04～PP-09、RT-03～RT-10、SM、JP | P2、L1全合格、該当するP項目の決定、T-01/T-03/T-04/T-05の準備 | 状態遷移の全遷移とprotocolの全項目が合格 |
| P4 | 異常系・電源 | CK-05、PW、LR-04～LR-06 | P3 | fault注入の全項目で出力が規定どおり（High-Zまたは継続） |
| P5 | 精度・環境 | PP-10～PP-12、EN | P3、T-02、恒温槽 | 0/25/40 ℃でR-013を含む全項目が合格 |
| P6 | 実機統合 | SY | P3、OI-001（ハーネス）完了 | 全接続先で受信・同期を確認、RMC=`V`時の挙動を記録 |
| P7 | 長時間・release判定 | LR-01～LR-03 | P4～P6 | §6の全合否条件が合格。不合格は理由と処置をOIへ登録 |

L1（UT）はP1と並行して始め、P3開始までに全項目を合格させる。

## 5. 試験項目

「対象」欄: 全数=受領した全基板、代表=1枚（環境・破壊的な試験は同じ1枚に固定）、host=L1。

### 5.1 L1 host単体試験（UT）

合格の目安として、UT対象モジュールの分岐網羅率90 %以上を本計画の目標とする（正本の要求ではない）。

| ID | 項目 | 方法 | 合否基準（出典） |
|---|---|---|---|
| UT-01 | CRC32C | 既知vector（`"123456789"`→`0xE3069283`）、空入力、分割計算と一括計算の一致 | reflected `0x82F63B78`、init/final XOR `0xFFFFFFFF`（protocol） |
| UT-02 | Jetson frame解析 | 正常3 command、ゴミbyte後のmagic再同期、version不正、`payload_length` > 48、全長64 byte超、CRC不一致、未定義command、予約bit、inter-byte 19 ms/21 ms、有効frameの全bit位置の1 bit反転、byte欠落・追加、random fuzz 10^6件以上（ASan/UBSan有効） | 不正frameで時刻・RTC・backupの変更要求が一度も出ない。crashしない（protocol） |
| UT-03 | `TIME_SET`の検証 | nanoseconds境界（999999999/1000000000）、`valid_for_ms` 0/1/5000/5001、期限切れ、Unix秒の範囲（P-17）、同じsequence・同じpayloadの再送、同じsequence・異なるpayload（P-12） | 範囲外を拒否する。同じ再送は同じ応答で二重適用しない（protocol） |
| UT-04 | 応答のpack | `TIME_STATUS` 26 bytes、little-endian、reserved=0、flags bit3=0、clock diagnostic=0、ACK/NACK 2 bytes | protocolのfield表と一致 |
| UT-05 | NMEA解析 | 正常RMC（GP/GN）、checksum不一致、`*`欠落、小文字hex、120文字超（UM982の測位後のRMCは82文字を超えるため、受信は120文字まで受け付ける。P-07）、途中切れ、status `V`、空のtime/date field、小数秒、`23:59:60`（P-17） | 不正文を有効にしない。status `A`以外をlockの根拠にしない |
| UT-06 | NMEA生成 | field数、checksum、CRLF、82文字以内、status A/V、P-09で決めたfield | NMEA 0183の書式、P-08/P-09 |
| UT-07 | VLP +1秒 | 2000-01-01～2099-12-31の全日について23:59:59→翌日00:00:00を、独立実装（Python `datetime`で生成した表）と照合する。月末、年末、2/28→2/29→3/1、checksum再計算 | 全件一致（design §8、R-003） |
| UT-08 | calendar⇔Unix秒変換 | RTC範囲全域の往復変換、境界値 | 全件一致 |
| UT-09 | 状態機械 | 全state×全eventの表を試す。各遷移後の出力方針（OE、RMC status）。`FAULT_CLOCK`から自動でPPSを再enableしないこと。5回連続正常でlock、4回で中断したらlockしないこと。PPS 1回欠落ではlock維持、2回連続欠落でholdover | 状態遷移表と一致（state machine） |
| UT-10 | DPLL制御則（simulation） | TCXO model（初期偏差±5 ppm、温度ramp、雑音）、GNSS PPS jitter、capture量子化20.833 ns、glitch、位相stepを与えて閉ループで回す | 全周期でPPS間隔が0.999900～1.000100秒（ARR 47999999±4800 count）（design §4.1）。lock後の位相誤差が誤差予算内。holdoverでは最後に検証済みの補正値を保持する |
| UT-11 | backup-domain検証 | register mockで、正常、signature/version不一致、application CRC異常、configuration CRC異常、`INITS=0`、RTCSEL/LSEON/LSERDY/RTCEN異常、calendar範囲外、SSRが単調でない、の各ケースを試す。hardwareが更新するbit（LSERDY、calendar、SSR）を変えてもconfiguration CRCが変わらないこと。書込み途中の全stepで電源断した場合を再現する | 異常はすべて「BDRST + `RTC_INVALID`」、正常はBDRSTしない。書込み途中のdataを有効と判定しない（state machine） |
| UT-12 | 静的検査・容量 | 3種buildの警告、静的解析、map fileのFlash/RAM使用量 | 警告ゼロ。Flash使用量がP-19の上限以内 |
| UT-13 | MCU上でのL1試験 | test build（selftest image）の`ut` commandで、UT-01～UT-12と同じsourceをCortex-M0+上で実行する | 全件合格。整列していないaccess、64-bit除算、整数幅の違いでhostと結果が変わらないこと |

### 5.2 受入・書込み経路（BU）

| ID | 項目 | 方法 | 合否基準（出典） | 対象 |
|---|---|---|---|---|
| BU-01 | 外観・実装 | U1刻印（N接尾辞なし）、U2/U14/X1の向き、BT1極性、F1 DNP・R3/D1実装 | BOMおよびDR-058/DR-060と一致 | 全数 |
| BU-02 | 通電前の短絡確認 | `VBUS_PROTECTED`-GND、`3V3`-GND、`VBAT_RTC`-GNDの抵抗 | 短絡なし | 全数 |
| BU-03 | 初回通電（電池なし、空flash） | USB給電で`3V3`電圧、VBUS電流、U3温度を測る | 3.3 V rail正常、VBUS < 100 mA（power_budget） | 全数 |
| BU-04 | 空flashでのDFU | USB接続で`dfu-util -l`。T-07中点biasで全出力pinを観測する | system bootloaderのDFUが見える。全出力High-Z（pin_assignment「初回bring-up手順」） | 全数 |
| BU-05 | SWD接続とID記録 | TP1～TP5の導通確認後、`DBGMCU_IDCODE`を読む | DEV_ID（RM0444で確認）が一致し、`REV_ID`を記録済み。Rev A（`0x1000`）ならES0548 §2.2.9の注意を適用（pin_assignment） | 全数 |
| BU-06 | DFUで初回書込み→起動 | 書込み後、SW1だけで起動しないことを確認し、USB抜き差しで起動させる | 電源の入れ直し後に新firmwareが起動する（RM0444 §2.5.4の`EMPTY`） | 全数 |
| BU-07 | option byte | 安定したUSB給電で`nBOOT_SEL=0`、`nBOOT1=1`を書き、reload完了まで給電を保つ。読み返す | 値が一致。`BOOT_LOCK`とRDPは既定値のまま（pin_assignment、firmware_requirements） | 全数 |
| BU-08 | BOOT+RESETでDFU | SW2を押したままSW1を押して離す。SWD使用後は全電源OFFを挟む | DFUが見え、その間も全出力High-Z | 全数 |
| BU-09 | CDC commandでbootloaderへ移行 | H-09 | DFUが見え、書込み後に正常起動する（pin_assignment、P-15） | 代表 |
| BU-10 | SWDでの復旧 | TP1～TP5から全消去・書込み | 復旧できる | 代表 |
| BU-11 | Flash容量とlinker設定 | linker scriptのFLASH 256 KB、flash書込みを使う場合はpage番号とbank境界（RM0444）を実機で確認 | 書込み・読出しが一致。容量がP-19の上限以内（DR-060） | 代表 |
| BU-12 | USB CDC console | WindowsとJetson（Linux）で列挙、H-01の応答、host側のport close/再open・host再起動後の復帰、hostが読まない状態でlogを出し続ける | 列挙・応答できる。基板resetなしで復帰する。hostが読まなくても時刻機能（PPS/NMEA/DPLL）が止まらない（R-015、design §4.5） | 全数（列挙のみ）、代表 |

### 5.3 Clock（CK）

| ID | 項目 | 方法 | 合否基準（出典） | 対象 |
|---|---|---|---|---|
| CK-01 | clock設定のregister監査 | 初期化後にSWDまたはH-01で読む: VOS=Range 1、`VOSF=0`、`FLASH_ACR` LATENCY=2、HSEBYP/HSEON/CSSON、PLLM=2/PLLN=16/PLLR=4/PLLQ=4、SYSCLK=PLL、USB clock=PLLQ、HSI48/CRS未使用、VREFBUF無効、`PVM_VDDIO2=110`、UCPD strobe実施、`RCC_BDCR`のLSEDRV=10・LSECSSON=0・RTCSEL=LSE、`TAMP_CR1`のITAMP4E=0 | 全項目一致（firmware_requirements「起動順序」「Clock・USB」「RTC・backup domain」、pin_assignment「firmware初期化の必須項目」） | 全数 |
| CK-02 | 初期化の順序 | debug buildでclock切替の各段階にTP9 pulseを出し、`3V3`立上りと同時にscopeで観測する。Flash wait state→SYSCLK切替の順序はcode reviewでも確認する | `3V3`立上りからHSE切替まで5 ms以上。VOS→`VOSF`確認→Flash 2 WS→PLL切替の順序（firmware_requirements） | 代表 |
| CK-03 | TCXO・comparator・PF0の波形 | U2 pin 3振幅、U14入力common-mode、PF0の周波数・振幅・duty・jitter | U2 pin 3 ≥ 0.8 Vp-p（U2仕様）、PF0 24 MHz、duty 45～55 %。0/40 ℃はEN-01で実施（design §8） | 全数（25 ℃） |
| CK-04 | TCXO自走偏差 | H-05でDPLLを止め、GNSS PPSに対するTP6 PPSの位相driftを1000秒以上測り、ppmへ換算する | 記録する（基板別の初期偏差。DPLLのcapture range検討とP-01に使う） | 全数 |
| CK-05 | clock fault | (a) H-07でCSS/PLL unlockの処理経路を起動する。(b) 代表1枚で実際にHSEを止める（候補: U14 +IN側のC11 padをGNDへ短絡してU14出力をLowに固定する。実施前に方法をレビューする） | `FAULT_CLOCK`へ遷移し、全`OE_N`=High、PPS/NMEA High-Z、CDCはHSIで応答する。clock回復後も自動でPPSを再enableしない（state machine、firmware_requirements） | 代表 |

### 5.4 GPIO・出力（IO）

| ID | 項目 | 方法 | 合否基準（出典） | 対象 |
|---|---|---|---|---|
| IO-01 | reset・起動・DFU中の出力 | T-07中点biasを全12出力に付け、電源投入、SW1 reset、watchdog reset、DFU移行、BOR（PW-04）を各10回以上行い、scopeのglitch triggerで観測する | firmwareがenableするまで全出力High-Zで、Low/Highのglitchがない（pin_assignment「試作機の電源順序制約」） | 全数（各1回）、代表（各10回以上） |
| IO-02 | `OE_N`の初期化手順 | code reviewとregister監査: 出力dataをHighにしてからoutput modeへ切り替えているか | Low glitchを出さない手順になっている（pin_assignment「firmware初期化の必須項目」） | host/代表 |
| IO-03 | 個別enable | H-02で7本の`OE_N`を1本ずつenableする | 対応するchannelだけが駆動される（R-017） | 全数 |
| IO-04 | 出力の電気特性 | 全12出力のHigh/Low電圧、立上り・立下り、6系統PPSのchannel間skew。J4はTP7/TP8へ各1.5 kOhmを付けて測る | J4はHigh > 3.0 V、Low < 1.2 V（全基板）。Mid-360のPPSは立上り1 V/us超を推奨。その他は記録（design §8、open_issues） | 全数 |
| IO-05 | PA6 `TP_FW_TRACE` | release buildでPA6のMODER=analog、PUPDR=no-pullを確認。debug buildで既定Lowから意図したpulseが出ることを確認。TP9開放のまま全機能が動くことを確認。release buildでtest hookのcommandを拒否することも確認する | firmware_requirements「Bring-up合否」、design §8 | 代表 |
| IO-06 | 未使用pinとremap | 未使用pinがanalog、PC13にTAMP/RTC_OUTを割り当てていない、SYSCFG remap bitがreset値 | pin_assignment | 代表 |
| IO-07 | VDDIO2 domain | USB列挙、AUX1/AUX2のTX/RX、VLP/Mid-360の`OE_N`制御 | すべて動作（pin_assignment「VDDIO2 domainのI/O」の診断表） | 全数 |
| IO-08 | status LED | 各状態で表示を確認 | P-16で決めた表示どおり | 代表 |

### 5.5 UART・NMEA（UA）

| ID | 項目 | 方法 | 合否基準（出典） | 対象 |
|---|---|---|---|---|
| UA-01 | 入力経路（`RXINV=1`） | J2 NMEA、J3、J7、J8へUSB-UARTから既知のbyte列を10^6 byte以上送る。未接続で起動したときに受信byteが出ないことも確認する | 誤りゼロ、未接続時のFE/ORE・受信ゼロ | 全数（短縮版）、代表 |
| UA-02 | 出力経路 | 6系統TXのbaud、極性（VLPだけ反転）、内容、channel間の混線 | 指定baudで正しく受信でき、VLPは反転している（R-002、R-017、P-08） | 全数 |
| UA-03 | 折返し | J3/J7/J8のpin 3→pin 4 loopback | 送信内容と受信内容が一致（R-018） | 全数 |
| UA-04 | NMEA内容 | T-01の真値時刻と全channelの出力を照合する。VLP以外は直前PPSの時刻、VLPは+1秒 | checksum正常。`GNSS_LOCKED`ではstatus A、holdoverではV。時刻が真値と一致（design §4.2、P-08、P-09、P-11） | 代表 |
| UA-05 | 送信タイミング | logic analyzerでPPSと6系統TXを同時に1000秒以上記録する（`GNSS_LOCKED`、holdover、`RESYNC`の各状態） | VLP: PPS立下り→NMEA開始 ≥ 50 ms、NMEA完了→次のPPS立上り ≥ 300 ms。Mid-360: PPS立上り後430 ms以内に開始（design §3.2/§3.3/§8、P-10） | 代表 |
| UA-06 | VLP +1秒の境界（実機） | T-01で日時を設定する: 12/31 23:59:5x（うるう年と平年）、2/28、2/29、月末。holdover中にもRTC/TIM2で同じ境界を通す | 日付の繰上げとchecksumが正しい（design §8、state machine） | 代表 |
| UA-07 | NMEA入力の異常 | T-01でchecksum異常、途中切れ、ゴミbyte、status `V`、baud違いの雑音、NMEA停止 | 不正文でlockしない。状態遷移表どおりに遷移する（design §8） | 代表 |
| UA-08 | 負荷時の余裕 | 6 TXを最大送信、4 RXへ連続受信、T-04でJetson frameを連続送信する。debug buildのTP9でISR時間を測る | ORE=0、capture取りこぼしゼロ、UA-05の時間窓を維持 | 代表 |

### 5.6 PPS・TIM2・DPLL（PP）

| ID | 項目 | 方法 | 合否基準（出典） | 対象 |
|---|---|---|---|---|
| PP-01 | TIM2設定の監査 | ARR=47999999（公称）、ARPE=1、CCR2=4800000、CH2 PWM mode 1 active high、CH1 input capture falling、PA5/PC5のAF2 | 全項目一致（pin_assignment「PPS/holdoverのtimer構成」） | 全数 |
| PP-02 | 生成PPSの波形 | TP6と各コネクタpin 2 | 周期1秒、High幅100 ms、立上りが基準（Q-011） | 全数 |
| PP-03 | 入力captureのedge | T-01でPPSのパルス幅を10 ms/100 ms/200 msに変えて捕捉位相を比べる | 捕捉位相がパルス幅によらず一定（立下りcaptureで正しいedgeを捉えている。design §4.1） | 代表 |
| PP-04 | 固定offsetの校正と位相差 | UM982 PPS（J2 pin 2）と各出力pin 2の立上り差を測り、基板ごとのoffsetを求めて保存する（P-06）。校正後に1時間以上計測する | 校正後の絶対値1 us以内（design §8、open_issues）。最大・平均・σを記録 | 全数 |
| PP-05 | 起動からlockまで | 電源投入からGNSS lockと出力enableまでの時間 | 5回連続検証の後にだけenableする。最初に生成PPSをstepで揃えるので、PPSとRMC=Aがそろってから約8秒（2026-10-10の屋外実測。design §4.1.1） | 代表 |
| PP-06 | slew制限と異常パルス | `RESYNC`中とlock獲得中のPPS間隔・パルス幅をT-03で全数記録し、NMEAの秒ラベルと照合する | 全間隔が0.999900～1.000100秒。短パルス・二重パルス・欠落・秒の逆行や飛びがゼロ（design §4.1、Q-017） | 代表 |
| PP-07 | 周波数追従 | T-01のPPS周波数をCK-04の実測範囲を超えて変える | P-01で決めた範囲で追従し、範囲外は異常として扱う | 代表 |
| PP-08 | GNSS PPSの異常 | T-01でPPSを1回欠落、2回連続欠落、glitch、余分なedge、位相step | 1回欠落ではlock維持、2回連続欠落でholdover。glitchを採用しない。位相stepはP-03に従う（Q-007） | 代表 |
| PP-09 | UM982固有の挙動 | 実UM982のantennaを外す。`CONFIG PPS`の極性と幅を確認する | antennaを外した後、PPSが約30秒続き、RMCが`V`になる間も規定どおりholdoverへ移る（NMEA invalidで判定）。極性がPOSITIVE（P-07） | 代表 |
| PP-10 | holdover精度（R-013） | P-18の時間だけlockした後、T-02でGNSSを切り、5分間の出力PPSと真値PPSの位相差をT-03で記録する。H-04の内部記録と並行して取る | 5分間を通して誤差が絶対値1 ms以下（R-013、design §6.6）。0/25/40 ℃で各5回以上（EN-01） | 代表 |
| PP-11 | GNSS復帰 | holdover 5分と1時間の後にT-02で復帰させる | 5回連続正常の後に`RESYNC`、±100 us/s以内で収束してlock（P-04）。異常パルスゼロ（design §8） | 代表 |
| PP-12 | 内部記録の妥当性 | H-04の値とT-03の外部測定を同時に取って比べる | 差が固定offsetの範囲内（以降の長時間試験でH-04を使えるようにするため） | 代表 |
| PP-13 | 出力の切替で切れない | J3/J4 pin 2をscopeの「正パルス幅 < 99 ms」triggerで見ながら、consoleの`out off`/`out on`と`FORCE_SET`（JP-05）を秒内のrandomな時点で各50回以上行う。TXはlogic analyzerでdecodeする | 短パルスゼロ、途中で切れた文ゼロ。enableした秒のNMEAはJ4だけ（P-10） | 代表 |

### 5.7 RTC・backup domain（RT）

| ID | 項目 | 方法 | 合否基準（出典） | 対象 |
|---|---|---|---|---|
| RT-01 | LSE発振余裕 | H-08で`RADD`を挿入して安全係数を測り、1 pF以下のprobeでOSC_IN側Vppを測る | `Sf ≥ 3`、`DL < 0.5 uW`を0 ℃・40 ℃・CR2032単独給電で満たす（design §8、firmware_requirements） | 代表（`RADD`の挿入は基板の改造になるため）。全数は`LSERDY`までの時間だけ記録 |
| RT-02 | 電源断中の保持 | 時刻設定→主電源OFF（1時間、24時間）→ON。H-01でTIM2秒とRTC秒を比べてLSE偏差も推定する | `RTC_VALID`で起動し、誤差がLSE ±20 ppm相当以内。LSE偏差を記録 | 全数（1時間）、代表（24時間） |
| RT-03 | 起動時検証の全ケース | SWD直接書込みとH-07で注入する: 正常、signature破壊、version不一致、application CRC異常、configuration CRC異常、`INITS=0`、電池なし。`BORRSTF`がreset flag clearより前に保存されることも確認する | 異常時はBDRSTをset→read-back→clearし、`RTC_INVALID`、`INIT_INVALID`、出力High-Z（state machine「文書段階の試験項目」、design §8） | 代表 |
| RT-04 | 書込み途中の電源断 | backup更新中にUSB電源をrandomなtimingで切る（1000回） | 書きかけを有効と判定しない。旧値が有効のまま残るか、無効と判定する（state machine） | 代表 |
| RT-05 | RTCの連続INIT | `SET_TIME`を0～2秒間隔で1000回送る | calendarが破損しない。`BYPSHAD` clear・`RSF`待ち・1 sessionでの書込み（ES0548 §2.9.1） | 代表 |
| RT-06 | LSE不起動 | H-07（代表1枚では実際にLSEを止める方法も検討する） | `FAULT_CLOCK`ではなく`RTC_INVALID`。GNSS/Jetsonで時刻が確立すれば出力できる（state machine） | 代表 |
| RT-07 | RTC-only起動 | `RTC_VALID`の状態で、GNSS/Jetsonなしで電源を入れる | SSRで秒内位相を初期化し、2回連続の単調更新を確認してから出力をenableする。RMC=`V`（R-012、state machine） | 代表 |
| RT-08 | RTC-only起動の後のGNSS取得 | RTC誤差を1 ms、50 ms、500 ms、1秒以上にしてからGNSSを与える | P-05/P-03で決めた挙動。異常パルスゼロ | 代表 |
| RT-09 | VBAT電流 | 主電源OFFでVBAT電流を測る | 2.0 uA以下（power_budget） | 全数 |
| RT-10 | 正常時にBDRSTしない | 正常状態で電源を100回入れ直す | 時刻を保持し、BDRSTが一度も起きない（state machine） | 代表 |

### 5.8 状態遷移（SM）

[`time_source_state_machine.md`](../hardware/overall/time_source_state_machine.md)の状態表にある全遷移を、実機で少なくとも1回ずつ通す。状態はH-01と`GET_STATUS`で確認する。

| ID | 項目 | 方法 | 合否基準（出典） | 対象 |
|---|---|---|---|---|
| SM-01 | `INIT_INVALID` | RTC無効、GNSS/Jetsonなしで起動 | 全channel High-Z。TCXO/TIM2（TP6）とCDCは動作 | 代表 |
| SM-02 | `GNSS_ACQUIRE` | T-01でPPSだけ、NMEAだけを与える。有効時刻がある場合とない場合 | 有効時刻があればholdover出力、なければHigh-Z。P-02のtimeoutで前の状態へ戻る | 代表 |
| SM-03 | `GNSS_LOCKED`への進入 | PPS + 有効NMEAを5回連続与える。4回で途切れさせるケースも試す | 5回連続でだけlockし、RMC=A | 代表 |
| SM-04 | `GNSS_LOCKED`→`TCXO_HOLDOVER` | PPS 2回連続欠落、NMEA timeout、NMEA invalid（V、checksum） | PPSが継続し、全channelがRMC=V、VLPの+1秒を維持（Q-007） | 代表 |
| SM-05 | `FAULT` | T-01でUTCの矛盾（NMEA時刻の飛び、日付の飛び）、P-03を超える位相差を与える | 全出力High-Z、状態を通知する。P-12で決めた方法でだけ抜ける | 代表 |
| SM-06 | 優先順位とJetson | GNSS無効中にSET→`JETSON_SET`→`TCXO_HOLDOVER`。その後GNSSを確立→`GNSS_LOCKED`。P-13の期限切れ | GNSS > Jetson > RTCの優先順位（R-011） | 代表 |
| SM-07 | statusを偽らない | 全試験のNMEA出力ログを走査する | `GNSS_LOCKED`以外でstatus Aが一度も出ない（`RESYNC`はP-09に従う）（state machine「基本方針」） | 代表 |

### 5.9 Jetson protocol（JP）

T-04を使ってUSB（J1）のCDC interface 2から行う（DR-061）。

| ID | 項目 | 方法 | 合否基準（出典） | 対象 |
|---|---|---|---|---|
| JP-01 | `GET_STATUS` | 各状態で要求する | 26 byteで全fieldが状態と一致（protocol） | 代表 |
| JP-02 | `INIT_INVALID`での`SET_TIME` | SETを送り、出力とRTCを観測する | 次の整数秒境界でstepし、適用完了まで出力High-Z。ACKはRTC更新とbackup commitの後に返る（state machine、protocol） | 代表 |
| JP-03 | 小offsetのslew | 有効時刻で動作中に±1～100 msのSET | ±100 us/s以内でslewし、UTCが逆行しない | 代表 |
| JP-04 | 大offsetの拒否 | 100 msを超えるoffsetのSET | `NACK_OFFSET_TOO_LARGE`、時刻は不変。応答内容はP-12 | 代表 |
| JP-05 | `FORCE_SET_TIME` | 大offsetでFORCE_SET | 全出力をHigh-Zにして次の秒境界でstepし、2秒連続の正常更新の後に再enable | 代表 |
| JP-06 | GNSS有効中 | `GNSS_LOCKED`でSET/FORCE_SET | `NACK_GNSS_ACTIVE`、RTCは不変 | 代表 |
| JP-07 | frame異常の注入 | 1 bit反転、byte欠落・追加、途中切断、inter-byte timeout、CRC不一致、未定義command、version不正 | 時刻・RTC・backupが不変で、次のmagicへ再同期する。3回連続errorはstatusへ記録するが時刻を無効にしない（protocol） | 代表 |
| JP-08 | 再送 | 同じsequence・同じpayloadを再送する | 同じ応答で二重stepしない（protocol） | 代表 |
| JP-09 | 範囲外・期限切れ | nanoseconds範囲外、`valid_for_ms`超過 | 拒否（protocol） | 代表 |
| JP-10 | 電源の入れ直し後の保持 | SET→電源OFF/ON→`GET_STATUS` | 時刻を保持し、`last source_id`/`last sequence`が正しい | 代表 |
| JP-11 | RTC書込み失敗 | H-07 | `NACK_RTC_WRITE`、出力High-Z（protocol） | 代表 |

### 5.10 電源（PW）

| ID | 項目 | 方法 | 合否基準（出典） | 対象 |
|---|---|---|---|---|
| PW-01 | 最大動作時の電流 | H-06で全12出力をHigh固定し各1.5 kOhmで負荷、USB通信連続、LED点灯で、VBUS電流、3.3 V droop、U3表面温度を測る | VBUS < 100 mA。結果を[`power_budget.md`](../hardware/overall/power_budget.md)の試作記録表へ書く | 全数（25 ℃） |
| PW-02 | USB切断・再投入 | `VBUS_PROTECTED`/`3V3`波形を観測しながら100回繰り返す | 正常に再起動し、意図しないresetや誤出力がない。RTCを保持する（open_issues） | 代表 |
| PW-03 | VBUS電圧範囲 | 4.75 Vと5.25 Vで全機能を確認する | 全機能動作（design §4.3） | 代表 |
| PW-04 | 緩い立上り・瞬断 | 可変電源でVBUSをゆっくり上下させる | BOR後にきれいに再起動し、その間出力High-Z。BOR設定を記録 | 代表 |
| PW-05 | 文書化した電源順序 | 本基板ON→接続先ON→出力enable→H-02で全High-Z→接続先OFF→本基板OFF | 外部機器OFF中に出力がHigh-Z、意図しないresetなし（design §8、open_issues） | 代表 |

### 5.11 環境（EN）

各温度で30分以上置いてから測る。

| ID | 項目 | 方法 | 合否基準（出典） | 対象 |
|---|---|---|---|---|
| EN-01 | 0/25/40 ℃の静的試験 | 各温度でCK-03、USB列挙（PLLQ 48 MHz）、RT-01、PP-10、PW-01を行う | 各試験の合否基準（design §8、open_issues） | 代表 |
| EN-02 | 温度変化中のholdover | 25 ℃でlock→GNSSを切ると同時に40 ℃（および0 ℃）へ恒温槽の最大rateで変える | 5分間の誤差が1 ms以下（R-013。誤差予算はpeak-to-peakの温度差を想定している） | 代表 |

### 5.12 実機統合（SY）

| ID | 項目 | 方法 | 合否基準（出典） | 対象 |
|---|---|---|---|---|
| SY-01 | Jetson | 中継基板を経由してJ3のPPSとNMEAをJetsonへ入れる。Jetson側の設定はCub_ROSの`scripts/install_host_settings.sh`による（2026-10時点はPPS=`/dev/pps1`、NMEA=`/dev/ttyTHS1` 9600 baud、NTPsec）。`ppstest`でPPSを、`ntpq -pn`で同期を確認する。USB CDC interface 2でT-04相当のclientを動かす | PPSを毎秒受信し、NTPsecがGNSS（`.GPS.`）へ同期する。SET/GETが動作する（R-008、R-010） | 代表 |
| SY-02 | VLP-32C | **接続前に**VLP側GPS入力端子の開放電圧を測る。接続後にPPS lock、反転NMEAのdecode、UTC +1秒を確認する。holdover中（RMC=V）のtimestampを記録する | 5 Vへのpull-upがない（DR-024）。PPS lock、時刻が+1秒込みで一致。RMC=V時の挙動を記録（open_issues） | 代表 |
| SY-03 | Mid-360 | Livox Viewer/SDKで時刻同期状態とtimestampを確認する。holdover中も確認する | GPS同期が成立する。RMC=V時の挙動を記録（open_issues） | 代表 |
| SY-04 | Spresense level board | J6からのPPS/NMEAを別基板側で受信する | 受信できる（R-009の範囲内で3.3 V出力まで） | 代表 |
| SY-05 | AUX1/AUX2 | loopbackと接続予定機器 | 送受信できる（R-018） | 代表 |
| SY-06 | 全機器の同時接続 | lock→holdover→復帰を通して各機器のtimestampを比べる | 各機器の時刻が揃っている。結果を記録 | 代表 |

### 5.13 長時間・堅牢性（LR）

| ID | 項目 | 方法 | 合否基準 | 対象 |
|---|---|---|---|---|
| LR-01 | lock連続 | 実UM982で72時間、T-03とT-05で全PPS・全NMEAを記録する | 異常パルス・checksum異常・時刻の不連続がゼロ。resetなし（reset要因で確認） | 代表 |
| LR-02 | holdover連続 | GNSSなし（RTC-only起動）で24時間 | 異常なく出力を続ける。driftを記録（R-013の対象は5分まで） | 代表 |
| LR-03 | GNSSの断続 | T-02で10秒～10分のrandomな間隔で切断・復帰を24時間繰り返す | 短パルス・二重パルス・欠落・秒の逆行や飛びがゼロ（design §8） | 代表 |
| LR-04 | Jetson frameの負荷 | T-04で正常frameと異常frameを24時間混ぜて送る | crashしない、意図しない時刻変更がない、stack使用量の最大値が一定 | 代表 |
| LR-05 | watchdog | H-07でmain loopを止める | IWDG resetの間は出力High-Z。再起動後はP-14どおり。reset要因を記録 | 代表 |
| LR-06 | power cycle | USB電源制御でrandomなtimingの電源断を1000回 | backup破損を有効と誤判定しない。誤出力がない | 代表 |

## 6. トレーサビリティ

### 6.1 要求（design §2）

| 要求 | 試験ID |
|---|---|
| R-001 PPS/NMEA配信 | PP-02、UA-02、UA-04、SY-01～SY-06 |
| R-002 VLP UART反転 | UA-02、SY-02 |
| R-003 VLP NMEA +1秒 | UT-07、UA-06、SY-02 |
| R-004 GNSS消失時の出力継続 | SM-04、PP-10、UA-04 |
| R-005/R-006 低コスト・部品区分 | firmware対象外（BOMで管理） |
| R-007 GH経由のUM982入力 | UA-01、PP-03、PP-09 |
| R-008 Jetson Developer Kit接続 | SY-01 |
| R-009 Spresense level shiftは対象外 | SY-04（3.3 V出力の受信まで） |
| R-010 Jetsonから時刻・RTC初期化 | UT-02～UT-04、JP-01～JP-11 |
| R-011 優先順位 GNSS > Jetson > RTC | UT-09、SM-06 |
| R-012 RTCで妥当な日時から起動 | RT-02、RT-07 |
| R-013 GNSS消失後5分で1 ms以下 | UT-10、PP-10、EN-01、EN-02 |
| R-014 USB Type-C給電 | BU-03、PW-01～PW-03 |
| R-015 USBでconsoleとfirmware更新 | BU-04、BU-06、BU-08、BU-09、BU-12、IO-07 |
| R-016 UM982へ給電しない | firmware対象外（回路で担保） |
| R-017 TXの個別制御 | IO-03、UA-02 |
| R-018 予備GH 2個 | UA-03、SY-05 |

### 6.2 正本の合否条件

正本に書かれた合否・試験項目のすべてを、いずれかの試験IDへ対応させる。

| 出典 | 合否条件（要約） | 試験ID |
|---|---|---|
| open_issues「試作合否試験」 | J4 1.5 kOhm負荷でHigh > 3.0 V、Low < 1.2 V（全基板） | IO-04 |
| 〃 | VLP側の開放電圧（5 V pull-upなし） | SY-02 |
| 〃 | VLP実機でPPS lock、反転NMEA、UTC +1秒 | SY-02 |
| 〃 | Mid-360/VLP-32Cのholdover中RMC=Vでのtimestamp | SY-02、SY-03 |
| 〃 | 0～40 ℃でGNSS消失後5分の誤差1 ms以下 | PP-10、EN-01、EN-02 |
| 〃 | GNSS復帰時に異常パルス・秒の逆行なし | PP-06、PP-11、LR-03 |
| 〃 | GNSS追従中のPPS位相差1 us以内（校正後） | PP-04 |
| 〃 | TCXO start-up、U2振幅、U14 common-mode、PF0、CSS fault（0～40 ℃） | CK-03、CK-05、EN-01 |
| 〃 | LSE `Sf ≥ 3`、`DL < 0.5 uW`、LSE不起動は`RTC_INVALID` | RT-01、RT-06 |
| 〃 | 0～40 ℃でPLLQ 48 MHzによるUSB列挙 | CK-01、EN-01 |
| 〃 | RTC連続INIT、option byte安全書込み、Jetson frame試験 | RT-05、BU-05、BU-07、JP-07、JP-08 |
| 〃 | 電源順序でback-power・誤出力・意図しないresetなし | IO-01、PW-05 |
| 〃 | USB切断時の波形と再投入時の起動 | PW-02 |
| 〃 | VBUS < 100 mA、VBAT ≤ 2.0 uA、droop、U3温度 | PW-01、RT-09 |
| firmware_requirements「起動順序」1～8 | `OE_N`、`BORRSTF`、BDRST、PVM/UCPD/VREFBUF、5 ms待ち、clock順序、PLLQ、PA6、enable順序 | IO-01、IO-02、RT-03、CK-01、CK-02、IO-05、SM-01 |
| firmware_requirements「Clock・USB」 | HSE 24 MHz/PLL設定、PLLQ 48 MHz（HSI48/CRS不使用） | CK-01 |
| 〃 | TIM2 ARR=47999999・ARPE・CCR2=4800000、補正はARRだけで行う | PP-01、UT-10 |
| 〃 | GNSS PPSはCH1 falling-edge capture | PP-01、PP-03 |
| 〃 | HSE/PLL/CSS faultで全出力High-Z、`FAULT_CLOCK` | CK-05 |
| firmware_requirements「RTC・backup domain」 | LSEDRV=10、LSECSSON=0、PC13にTAMP/RTC_OUTを割り当てない | CK-01、IO-06 |
| 〃 | 再INIT時の`BYPSHAD` clear・`RSF`待ち、1 sessionでのcalendar書込み | RT-05 |
| 〃 | backup payloadはCRCを最後にcommit | UT-11、RT-04 |
| 〃 | configuration CRCはsoftware設定bitだけを対象にする | UT-11 |
| 〃 | LSE不起動は`RTC_INVALID`で`FAULT_CLOCK`ではない | RT-06 |
| 〃 | GNSS未同期のTCXO補正値でR-013を保証しない | PP-10（P-18の前提条件） |
| firmware_requirements「DPLL・出力」 | TP9に製品機能を依存させない | IO-05 |
| 〃 | GNSS PPS/NMEAの5回連続検証でlock | UT-09、SM-03、PP-05 |
| 〃 | GNSS喪失時は最後に検証済みのARR補正を保持し、RMC=Vで出力 | UT-10、SM-04、UA-04 |
| 〃 | 復帰時の補正±100 us/s以内、異常パルス・UTC逆行なし | PP-06、PP-11 |
| 〃 | 追従中の位相差1 us以内、holdover 5分で1 ms以内（0～40 ℃） | PP-04、PP-10、EN-01 |
| firmware_requirements「Bring-up合否」 | SWD導通と`REV_ID`の記録、安定給電 | BU-05、BU-07 |
| 〃 | 0～40 ℃のU2/U14/PF0/USB | CK-03、EN-01 |
| 〃 | LSE `Sf`/`DL` | RT-01 |
| 〃 | fault注入、CRC破損、RTC連続INIT、Jetson破損、GNSS喪失・復帰を自動試験へ含める | CK-05、RT-03、RT-05、JP-07、PP-10、PP-11、LR-03 |
| 〃 | PA6の通常build/debug build | IO-05 |
| state machine「文書段階の試験項目」 | backup各異常、電池なし | RT-03 |
| 〃 | LSE不起動 | RT-06 |
| 〃 | RTC連続INIT | RT-05 |
| 〃 | clock初期化と波形 | CK-01～CK-03 |
| 〃 | HSE停止、PLL unlock、TCXO異常 | CK-05 |
| 〃 | PPSだけ、NMEAだけ、checksum異常、RMC=V、両方喪失 | SM-02、SM-04、UA-07、PP-08 |
| 〃 | GNSS喪失5分後の誤差1 ms以下 | PP-10 |
| 〃 | 復帰時の異常パルス・UTC逆行なし | PP-06、PP-11 |
| 〃 | Jetson SETの初期step、小offset slew、大offset拒否、FORCE_SET | JP-02～JP-05 |
| 〃 | holdover中のVLP +1秒、日付繰上げ、checksum | UA-06 |
| 〃 | VLP-32C/Mid-360のRMC=V受信時の挙動 | SY-02、SY-03 |
| jetson_time_protocol「合否試験」 | 1 bit反転、欠落、追加、切断、timeout、CRC不一致で時刻不変 | UT-02、JP-07 |
| 〃 | 同一sequenceの再送で二重stepしない | UT-03、JP-08 |
| 〃 | 期限切れ、nanoseconds範囲外、GNSS active、大offsetの拒否 | UT-03、JP-04、JP-06、JP-09 |
| 〃 | SET/FORCE_SET/GET_STATUSの正常応答と電源の入れ直し後の保持 | JP-01、JP-02、JP-05、JP-10 |
| design §8 | 出力電圧、edge、PPSのchannel間skew | IO-04 |
| 〃 | J4 1.5 kOhm負荷とVLP実機 | IO-04、SY-02 |
| 〃 | VLPの反転、NMEA完了→PPS 300 ms以上、PPS立下り→NMEA 50 ms以上 | UA-02、UA-05 |
| 〃 | Mid-360の3.3 V、PPS後430 ms以内のNMEA開始 | IO-04、UA-05 |
| 〃 | 23:59:59、月末、年末、うるう年 | UT-07、UA-06 |
| 〃 | checksum異常、部分受信、PPSだけ消失、NMEAだけ消失、完全消失 | UA-07、PP-08、SM-02、SM-04 |
| 〃 | 0～40 ℃でholdover 1 ms以下 | PP-10、EN-01 |
| 〃 | U2/U14/PF0とclock初期化、HSE CSS | CK-01～CK-03、CK-05 |
| 〃 | 0～40 ℃のUSB列挙 | EN-01 |
| 〃 | LSE `Sf`/`DL` | RT-01 |
| 〃 | 主電源OFF/ON、電池なし、`INITS=0`、CRC異常でBDRST | RT-03 |
| 〃 | PPS位相差1 us以内 | PP-04 |
| 〃 | 3.3 V電流、droop、U3温度 | PW-01 |
| 〃 | VLP側の開放電圧 | SY-02 |
| 〃 | holdover中RMC=Vでのtimestamp追従 | SY-02、SY-03 |
| 〃 | 切替時に短パルス・二重パルス・秒飛びなし | PP-06、PP-11、PP-13、LR-03 |
| 〃 | 電源順序でHigh-Z、外部入力を駆動しない | PW-05、IO-01 |
| 〃 | PA6の通常build/debug build、TP9なしで全機能 | IO-05 |

## 7. 記録と不具合管理

- 基板にS/Nを付け、基板ごとに記録票を作る。記録する値: S/N、`REV_ID`、UID、TCXO自走偏差（CK-04）、PPS固定offset（PP-04）、PF0 duty、J4電圧（IO-04）、LSE `Sf`/`DL`、VBAT電流、VBUS電流。
- 試験結果ごとに、firmwareのbuild ID、build種別、温度、使用機材、生データ（log、波形、T-03の記録）を残す。保存場所の案: `firmware/test_results/<S/N>/<日付>_<build ID>/`。大きな生データはgit管理外へ置き、要約と合否だけをcommitする。
- 不合格は原因を「firmware」「基板（回路・実装）」「仕様」に分ける。
  - firmware: issueを起こして修正し、該当試験とL2 smokeを再実行する。
  - 基板・仕様: [`open_issues.md`](../hardware/overall/open_issues.md)へOIとして登録する。仕様を決めたらdecision_logへDRとして記録し、正本へ反映する。
- 測定値の記録先が正本にある場合（power_budgetの試作記録表など）は正本も更新する。

## 8. 最初に行う作業

1. §2のP-03/P-05/P-11/P-12/P-15を決める（firmware構造に影響する）。他のP項目は該当試験までに決める。
2. P0/P1を全基板で行い、`REV_ID`を記録する。option byteはBU-05を終えてから書く。
3. bring-up firmwareを作る: 安全なGPIO初期化（`OE_N` High）→clock→USB CDC→H-01→LED。並行してL1の基盤（host build、CRC32C、NMEA、Jetson frame）を作る。
4. T-07治具とT-06の最小構成を作り、P2を行う。
5. P3の前にT-01、T-03、T-04、T-05を作る。T-02と恒温槽はP5までに用意する。
6. L3統合試験の前にOI-001（ハーネスの相手側情報）を解決する。
