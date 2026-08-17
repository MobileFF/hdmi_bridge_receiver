# ビルドガイド

## 前提環境

- pico-sdk チェックアウト一式(RP2350/`pico2`ボードサポートを含むバージョン)。
  `build.sh`はデフォルトで`$HOME/projects/micropython/lib/pico-sdk`を参照する
  — MicroPythonのrp2ポートビルドで使っているものと同じチェックアウトを再利用
  する設計。別の場所にある場合は、`build.sh`内の`PICO_SDK_PATH`を書き換える
  か、実行前に環境変数として上書きする:

  ```bash
  PICO_SDK_PATH=/path/to/pico-sdk ./build.sh
  ```

  (ただし現状の`build.sh`は内部で`export PICO_SDK_PATH=...`と固定しているため、
  別パスを使う場合はスクリプトを直接編集するのが確実。)

- CMake 3.13以上、arm-none-eabi-gcc等の通常のpico-sdkビルドツールチェーン。

このビルドは、送信側プロジェクト(PB-1000_emu_AG2/MSX_emu_pico2)の
MicroPythonビルドとは完全に独立している。

## 通常ビルド

```bash
./build.sh
```

内部で行っていること:

1. `build/`ディレクトリを作り直す(既存があれば削除)。
2. `cmake -DPICO_BOARD=pico2 ..`
3. `make -j$(nproc)`
4. 生成された`hdmi_bridge_receiver.uf2`を`firmware/`ディレクトリへコピー。

ソースを変更していない場合は、ビルドせずに
[`firmware/hdmi_bridge_receiver.uf2`](../firmware/)(リポジトリに含まれる
ビルド済みファイル)をそのまま書き込んでもよい。

## 診断ビルド(HSTX出力のみ、SPI受信を無効化)

配線後に「HDMI信号がまったく出ない」場合、原因がHSTX初期化側にあるのか
SPI0スレーブ受信側(GP0/1/2の配線)にあるのかを切り分けるためのビルド。
SPI受信を無効化し、画面中央に固定の白い矩形を表示するだけの最小構成になる。

```bash
mkdir -p build_diag && cd build_diag
cmake -DPICO_BOARD=pico2 -DHDMI_ENABLE_SPI_RX=0 ..
make -j$(nproc)
```

`build_diag/hdmi_bridge_receiver.uf2`が生成される。これを書き込んで白い
矩形が正しい位置に表示されれば、HSTX/HDMI配線側は健全 — 原因はSPI0受信側
(配線・SPIモード等)に絞り込める。

## 書き込み

1. Pico 2の**BOOTSELボタンを押しながら**USB接続。
2. `RPI-RP2`という名前のドライブが出現する。
3. `firmware/hdmi_bridge_receiver.uf2`(または診断ビルドの場合は該当ファイル)
   をそのドライブへコピー(ドラッグ&ドロップ)。
4. 自動的に再起動し、HDMI出力が始まる。

## トラブルシューティング

| 症状 | 確認事項 |
|---|---|
| HDMI信号が全く出ない(モニタがNO SIGNAL) | 診断ビルドでHSTX単体を確認。ダメならHDMIアドオンの配線・実装を確認 |
| 診断ビルドでは白矩形が出るが通常ビルドで何も映らない | SPI配線(GP0/1/2)、送信側のSPIモード(モード3必須)、CS配線を確認 |
| 左上のマーカーが変化しない | 送信側からパケットが届いていない。配線・送信側コードを確認 |
| マーカーは変化するが映像が乱れる/位置がおかしい | プロトコル仕様([protocol.md](protocol.md))通りにヘッダーを組んでいるか確認。特にwidth/height/scaleの値、payload_lenの計算 |
| 起動直後は問題ないが数分〜数十分でNO SIGNALになる | 送信側のSPI送信コードがCore0のISR相当の処理内で重い処理をしていないか(このプロジェクト自体は実機検証済みだが、送信側の実装に問題がある場合がある) |
