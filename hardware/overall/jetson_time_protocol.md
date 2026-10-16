# Jetson時刻設定プロトコル

## 目的

JetsonからUSB CDC経由でSTM32へ時刻を設定・照会するframeを定義する。CRC、長さ、sequence、範囲を検証する。この検証はtransportがUARTだったときに、STM32G0B1 errata ES0548 §2.11.1（noisyなRXでのdata corruption）を考えて決めたもので、USBへ移した後も変えない（DR-061）。

## Transport

- **USB（J1）のCDC interface 2**（DR-061）。interface 0はconsoleである。Linuxでは`/dev/serial/by-id/usb-cub2026_timing_sync_if_<UID>-if02`として見える。参照実装は[`../../firmware/tools/jetson_time.py`](../../firmware/tools/jetson_time.py)
- J3のUART（3.3 V、8N1）はNMEA送信だけに使い、本protocolには使わない（P-11）
- byte order: little-endian
- 最大frame長: 64 bytes
- inter-byte timeout: 20 ms。timeoutしたframeは破棄する
- 同じ`sequence`と同じpayloadの再送は同じ応答を返し、時刻を二重適用しない
- host側がDTRを立ててportを開いていないと、STM32は応答を送らない

## Frame

| Offset | Size | Field | 内容 |
|---:|---:|---|---|
| 0 | 2 | magic | `0x54 0x53`（`TS`） |
| 2 | 1 | version | `0x01` |
| 3 | 1 | command | 下表 |
| 4 | 2 | payload_length | 0～48 |
| 6 | 2 | sequence | requestごとにincrement。応答はrequestの値をそのまま返す（STM32はincrementを検査しない） |
| 8 | N | payload | command別 |
| 8+N | 4 | CRC32C | magicからpayload末尾まで。reflected Castagnoli polynomial `0x82F63B78`、init/final XOR=`0xFFFFFFFF` |

magic、version、length、CRC32Cのいずれかが不正なframeは適用せず、受信同期を次のmagicまで取り直す。予約bitまたは未定義commandも適用しない。未定義commandには`NACK_UNKNOWN_COMMAND`、payloadの付いた`GET_STATUS`には`NACK_BAD_LENGTH`を返す。

## Command

| 値 | 名前 | Payload | 応答 |
|---:|---|---|---|
| `0x01` | `GET_STATUS` | なし | `STATUS` |
| `0x02` | `SET_TIME` | `TIME_SET` | `ACK`または`NACK` |
| `0x03` | `FORCE_SET_TIME` | `TIME_SET` | `ACK`または`NACK` |
| `0x81` | `STATUS` | `TIME_STATUS` | なし |
| `0x82` | `ACK` | request command、result code | なし |
| `0x83` | `NACK` | request command、error code | なし |

`TIME_SET` payload:

| Field | Size | 条件 |
|---|---:|---|
| Unix seconds | 8 | signed 64-bit UTC、leap secondを重複秒として送らない。RTCが表せる2000～2099年の範囲に限る（範囲外は`NACK_OUT_OF_RANGE`） |
| nanoseconds | 4 | 0～999999999 |
| valid_for_ms | 4 | 1～5000。受信完了からの有効期限 |
| source_id | 4 | Jetson側bootごとに変える識別子 |

STM32はframe受信完了時刻をcaptureし、範囲、CRC、期限、sequenceを検証する。GNSS有効中のSET/FORCE_SETは`NACK_GNSS_ACTIVE`とする。通常SETのoffsetが100 ms超なら`NACK_OFFSET_TOO_LARGE`、FORCE_SETは出力をHigh-Zにしてから状態遷移文書の規則で適用する。

`TIME_STATUS` payload:

| Field | Size | 内容 |
|---|---:|---|
| Unix seconds | 8 | 現在UTC。時刻が無効なときは0 |
| nanoseconds | 4 | 0～999999999。時刻が無効なときは0 |
| state | 1 | `0=INIT_INVALID, 1=GNSS_ACQUIRE, 2=GNSS_LOCKED, 3=JETSON_SET, 4=TCXO_HOLDOVER, 5=RESYNC, 6=FAULT, 7=FAULT_CLOCK` |
| flags | 1 | bit0=`RTC_VALID`、bit1=GNSS valid、bit2=clock fault、bit3=reserved（0）、bit4=LSE ready |
| reserved | 2 | 0固定 |
| last source_id | 4 | 最後に適用したSET。未適用なら0。RAMだけに持つので、resetや電源断で0に戻る |
| last sequence | 2 | 最後に適用したSET。未適用なら0。resetや電源断で0に戻る |
| clock diagnostic | 2 | DR-050では0固定。将来のclock診断用予約field |
| last error | 2 | 下の表 |

payloadはpacked 26 bytesとし、firmwareでsizeをstatic assertionする。`ACK/NACK` payloadはrequest command 1 byte、result/error code 1 byteの2 bytesとする。

result/error codeの値（[`../../firmware/core/include/ts_jetson_proto.h`](../../firmware/core/include/ts_jetson_proto.h)と同じ）:

| code | 名前 | 返す場面 |
|---:|---|---|
| `0x00` | OK | ACK |
| `0x01` | `NACK_GNSS_ACTIVE` | GNSSでlock中（`GNSS_LOCKED`、`RESYNC`）のSET/FORCE_SET |
| `0x02` | `NACK_OFFSET_TOO_LARGE` | 時刻が有効なときの通常SETで、offsetが100 ms超 |
| `0x03` | `NACK_RTC_WRITE` | stepの適用が8秒以内に終わらない、`FAULT_CLOCK`中のSET |
| `0x04` | `NACK_EXPIRED` | 受信完了から`valid_for_ms`を過ぎた |
| `0x05` | `NACK_OUT_OF_RANGE` | nanoseconds、`valid_for_ms`、Unix秒が範囲外 |
| `0x06` | `NACK_BAD_LENGTH` | payload長が違う |
| `0x07` | `NACK_UNKNOWN_COMMAND` | 未定義command |

`last error`は、`0x0000`=none、`0x0001`～`0x00FF`=直近に返したNACKのcode、`0x0101`=frame CRC、`0x0102`=frame length、`0x0103`=frame version、`0x0104`=inter-byte timeout。一度でもNACKを返すと、以後はframe errorよりNACKのcodeを優先して出し、resetまで消えない。

## エラー処理

- CRC error、length error、timeout、未定義commandでは時刻・RTC・backup registerを変更しない
- 3回連続のframe errorはstatusへ記録するが、それだけで有効時刻を無効化しない（現在のfirmwareは連続回数をparserの中で数えるだけで、statusにもconsoleにも出さない）
- SET適用後はRTC read-backとcalendar範囲を検証し、失敗時は`NACK_RTC_WRITE`として出力をHigh-Zにする
- ACKを返す時点はSETの種類で違う
  - step（時刻が無効なときのSET、FORCE_SET）: 次の整数秒境界で適用し、RTC更新とCRC付きbackup register commitが済んでからACKを返す。実測で約7秒かかる。8秒以内に終わらなければ`NACK_RTC_WRITE`を返す。待っている間に同じrequestを再送しても応答は1回だけである。待っている間に別のstepのSETが来ると前のSETは打ち切られ、現在のfirmwareでは前のSETに応答が返らない（不具合。host側は応答を待ってから次のSETを送る）
  - slew（時刻が有効なときの通常SET、offset 100 ms以内）: RTCを書かずにすぐACKを返す。RTCは、その後RTCの秒境界が生成PPSから5 ms以上ずれた時点で書き直す
  - LSEが動いていないときは、RTCを書かずにACKを返す

## 合否試験

- 1 bit反転、byte欠落、byte追加、切断、timeout、CRC一致しないframeを注入して時刻が変わらないこと
- 同一sequence再送で二重stepしないこと
- 期限切れ、範囲外nanoseconds、GNSS active、大offsetの拒否を確認すること
- `SET_TIME`、`FORCE_SET_TIME`、`GET_STATUS`の正常応答とpower-cycle後の保持を確認すること
