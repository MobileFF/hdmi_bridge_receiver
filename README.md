# hdmi_bridge_receiver

汎用HDMI出力ブリッジ受信ファームウェア。第2のRaspberry Pi Pico 2（RP2350）に
書き込み、本体側のマイコン（別のPico/Pico Wなど）からSPI経由で送られてくる
コンパクトなフレームデータを受信し、RP2350のHSTXペリフェラルでDVI/HDMI互換
信号として出力する。

送信側のプロトコルが完全に自己記述式なため、**このファームウェア自体は
どの本体エミュレータ／プロジェクト専用でもない**。現在
[PB-1000_emu_AG2](../PB-1000_emu_AG2/)（カシオ PB-1000ポケコンエミュレータ）と
[MSX_emu_pico2](../MSX_emu_pico2/)（MSX1エミュレータ）の2プロジェクトで、
送信側を書き込み直すだけでこの同一ファームウェアを共有している。

## ドキュメント

| ドキュメント | 内容 |
|---|---|
| [doc/protocol.md](doc/protocol.md) | **プロトコル完全仕様**(全パケット種別のバイトレベル定義、実例付き) |
| [doc/hardware_guide.md](doc/hardware_guide.md) | 必要なハードウェア・配線・実機検証済みの注意点 |
| [doc/build_guide.md](doc/build_guide.md) | ビルド・書き込み・トラブルシューティング |
| [doc/architecture.md](doc/architecture.md) | 内部実装(Core0/Core1分離、ウォッチドッグ等)の設計ノート |
| [demo_sender/](demo_sender/) | 汎用MicroPython送信サンプル(プロトコルの動作確認・学習用) |

*(英語版ドキュメントは `_en.md` サフィックスのファイルを参照してください)*

## クイックスタート

1. Raspberry Pi Pico 2(RP2350) + HDMI出力アドオン(PICO-HDMI-PLUS等、
   [doc/hardware_guide.md](doc/hardware_guide.md)参照)を用意する。
2. ファームウェアを用意する — 2通りの方法がある:
   - **ビルド済みを使う**: [`firmware/hdmi_bridge_receiver.uf2`](firmware/)
     をそのままBOOTSEL経由で書き込む。
   - **自分でビルドする**: `./build.sh`(下記) → `firmware/hdmi_bridge_receiver.uf2`
     が生成される。ソースを変更した場合はこちら。
3. [demo_sender/](demo_sender/) を別のPicoで動かして動作確認、または既存の
   送信側プロジェクト(PB-1000_emu_AG2/MSX_emu_pico2)の`[hdmi]`設定を有効化する。

```bash
./build.sh
```

## 既知の制限事項

- **出力解像度は640x480@60Hz固定**。他の解像度・リフレッシュレートには対応
  していない。
- **映像のみ、音声非対応**。HDMIケーブル経由での音声出力機能はない。
- **片方向リンク、ACK無し**。送信側から受信側への一方通行のプロトコルで、
  受信側が実際に受信できているか(存在するか)を送信側が確認する手段はない
  ([demo_sender/](demo_sender/)等で目視確認するしかない)。
- **1対1想定**。1台の送信側Picoにつき受信側Pico 2 1台を想定した設計。
  複数の受信側へ同時配信するようなファンアウトは想定していない。
- **ペイロード/キャンバスサイズに上限がある**。`PKT_FRAME`のwidth/heightは
  256x192まで、コマンド列(`PKT_TEXT_CMDS`/`PKT_BEZEL_CMDS`)のペイロードは
  49,152byteまで([doc/protocol.md](doc/protocol.md)の「実装上の制約」参照)。
- **受信側はRP2350専用**。RP2040(無印Raspberry Pi Pico/Pico W)ではHSTX
  ペリフェラルが無いため動作しない。送信側は任意のマイコンでよい。

## 開発の背景

このファームウェアはもともとMSX_emu_pico2プロジェクト内で
`hdmi_bridge/phase2_receiver/` として開発され(実機検証の経緯は
`MSX_emu_pico2/doc/hdmi_bridge_phase2_report.md`、初期の検討過程は
`MSX_emu_pico2/hdmi_bridge/README.md` に残っている)、後にPB-1000_emu_AG2
向けに移植・汎用化された。プロトコルが完全に自己記述式になったことで
どちらのプロジェクト専用でもなくなったため、共有の独立プロジェクトとして
切り出した。

## ライセンス

MIT License — [LICENSE](LICENSE)参照。フォントデータ(`font_petme128_8x8.h`)
とHSTX/DVI出力コードの一部は、それぞれ元のライセンス表示を保持した上で
サードパーティのコードを取り込んでいる(詳細はLICENSEファイル内に記載)。
