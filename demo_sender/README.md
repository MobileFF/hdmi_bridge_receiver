# デモ送信サンプル

`demo_sender.py`は、特定のエミュレータプロジェクトに一切依存しない、素の
MicroPython実装によるプロトコルのデモ送信スクリプト。`hdmi_bridge_receiver`
を単体で試したい場合や、プロトコルの実装方法を学びたい場合に使う。

## 必要なもの

- MicroPythonが書き込まれた任意のPico/Pico W/Pico 2(送信側。受信側と
  同じRP2350である必要はない)
- `hdmi_bridge_receiver`を書き込んだPico 2(受信側、[../doc/build_guide.md](../doc/build_guide.md)参照)
- 両者を[../doc/hardware_guide.md](../doc/hardware_guide.md)の配線で接続

## 実行方法

配線が済んだら、送信側Picoへ`demo_sender.py`を転送して実行する:

```bash
mpremote connect <ポート> run demo_sender.py
```

または、REPL上で:

```python
>>> import demo_sender
>>> demo_sender.main()
```

Ctrl-Cで停止できる。

## デモの内容

約4秒(40フレーム×100ms)ごとに3つのシーンを順に切り替える:

1. **色帯 + ベゼル** — `PKT_FRAME`(bpp=8直接色)で8色の横スクロールする
   縦帯を表示しつつ、`PKT_BEZEL_CMDS`で枠を重ねる。ベゼルとゲーム画面が
   同じ論理座標系・拡大率で送られ、受信側で独立に中央寄せされても画面上
   では同じ中心を共有して重なる様子を確認できる。
2. **パレットストライプ + ベゼル** — `PKT_PALETTE`で16色パレットを送り、
   `PKT_FRAME`(bpp=4パレット参照)で斜めストライプを表示。
3. **メニュー風画面** — `PKT_TEXT_CMDS`で矩形塗り+文字列描画を組み合わせた
   UI風の画面を表示(カウンタが毎フレーム更新される)。

## 自分のプロジェクトへの組み込み方

`demo_sender.py`内の`HDMIBridge`クラスと`cmd_rect()`/`cmd_text()`
ビルダー関数は、そのまま自分の送信側コードへコピーして使える(依存関係は
`machine.SPI`/`machine.Pin`のみ)。パケットの詳細仕様は
[../doc/protocol.md](../doc/protocol.md)を参照。

C(pico-sdk)で実装する場合は、実プロジェクトでの実装例
([PB-1000_emu_AG2/src/lcd_controller.c](../../PB-1000_emu_AG2/src/lcd_controller.c)、
[MSX_emu_pico2/src/msx/msx_core.c](../../MSX_emu_pico2/src/msx/msx_core.c))
を参照するとよい。
