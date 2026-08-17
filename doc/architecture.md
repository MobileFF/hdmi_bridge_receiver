# 内部実装ノート

`main.c`の内部設計と、実機検証で得られた制約について。プロトコル仕様は
[protocol.md](protocol.md)、配線は[hardware_guide.md](hardware_guide.md)を参照。

## 全体構成: Core0(受信) / Core1(描画)の分離

RP2350の2コアを以下のように分担させている:

- **Core0**: SPI0スレーブのDMA受信割り込みハンドラ(`spi_rx_dma_irq_handler()`)
  と、HSTXスキャンアウト用DMA割り込みハンドラ(`hstx_dma_irq_handler()`)。
- **Core1**: 受信したパケットの実際の描画処理(`core1_copy_loop()`)。

**この分離は実機検証で判明した制約に基づく必須設計**: Core0のSPI受信割り込み
ハンドラ内でmemcpy等の重い処理(例: フレームバッファへのピクセルコピー)を
行うと、HSTXの走査線再設定(約32μs周期で発生する)を妨げてしまい、数分〜
数十分単位でHDMI信号を失う(NO SIGNAL)という不具合が実機で確認された。

そのため、Core0のISRは「DMAの再アーム」と「Core1へのコア間FIFO通知」以外の
処理を一切行わない。実際の重い処理(ピクセル展開・フォント描画等)はすべて
Core1へオフロードされる。

## SPI受信: ダブルバッファ+2段階ヘッダー/ペイロード受信

`scratch_buf[2][MAX_PAYLOAD]`という2面のバッファをピンポン方式で使う:

1. 起動時、8byteヘッダーを受信する状態でDMAを待機させる。
2. ヘッダー受信完了(DMA完了割り込み)で、パケット種別を見て本体ペイロードの
   受信バイト数を決定し、`scratch_buf[現在の面]`へ向けてDMAを再アームする。
3. ペイロード受信完了で、`buf_meta[]`(下記)へメタ情報を確定コピーし、
   面を切り替えて、Core1へコア間FIFOで通知する。
4. 次のヘッダー受信へ戻る(1へ)。

DMAが書き込み中でない方の面をCore1が読み出す設計のため、受信と描画が
並行して進められる。

## Core0→Core1の情報伝達: `frame_meta_t`構造体

初期実装では、Core1への通知メッセージ(32bit)自体にwidth/height/scale等を
ビットパックしていたが、`PKT_TEXT_CMDS`/`PKT_BEZEL_CMDS`の`payload_len`
(可変長)を格納する余地がなくなったため、`buf_meta[2]`という
`volatile frame_meta_t`配列に切り替えた:

```c
typedef struct {
    uint8_t  kind;          // KIND_PIXEL/KIND_TEXT/KIND_BEZEL
    uint8_t  bpp;
    uint16_t width;
    uint16_t height;
    uint8_t  scale;
    uint16_t payload_len;
} frame_meta_t;
```

Core0はペイロード受信完了時に`buf_meta[面番号] = pending_meta`を代入して
から、コア間FIFOへ「面番号」だけを送る(SIOのFIFO push/popはコア間の順序
保証を伴う操作のため、これで十分)。Core1は面番号を受け取ってから
`buf_meta[面番号]`を読む。

## 表示ウィンドウの中央寄せ(`KIND_*`)

[protocol.md](protocol.md)の該当セクション参照。実装上のポイントは、
「これまで受信した最大サイズ」の追跡(`max_w[3]`/`max_h[3]`)と、「直前に
実際に描画した領域」の追跡(`last_left[3]`/`last_top[3]`/`last_ww[3]`/
`last_wh[3]`)の両方を、`KIND_PIXEL`/`KIND_TEXT`/`KIND_BEZEL`の3種別で
完全に独立させている点。

これは以下の2つの不具合を実機で踏んだ末の設計:

1. 単一のmax_w/max_hで全種別を追跡すると、大きいコンテンツ(メニュー)を
   一度表示した後、小さいコンテンツ(ゲーム画面)が左上に寄る。
2. 単一のlast_left/last_top等で全種別を追跡すると、ゲーム画面の新しい
   フレームが、直前に描画したベゼルの領域を(サイズが違うという理由で)
   誤ってクリアしてしまい、ベゼルが消えて見える。

各種別は「互いに重なって共存しうる別々のレイヤー」として扱われ、それぞれ
自分自身の直前の描画領域だけをクリアしてから次を描く設計にすることで、
両方解決している。

## ホットスワップ監視(`hdmi_rx_watchdog_cb()`)

送信側Pico2を電源を落とさず挿し替えると、ちょうどヘッダーやペイロードの
途中でSPIクロックが止まることがある。DMAは「あとN byte来たら完了」という
形で待っているだけなので、その続きが物理的に来なくなると完了割り込みが
永久に発生せず、受信が止まったままになる。

これを検出するため、`add_repeating_timer_ms()`で約50ms間隔のタイマーを
仕掛け、DMAの残り転送数(`dma_hw->ch[SPI_RX_DMA_CHAN].transfer_count`)を
監視する:

- 「ヘッダー待ちで1byteも受信していない」状態(=何も繋がっていないか、
  次のパケットを待っているだけの正常なアイドル状態)は、いつまで続いても
  異常とみなさない。
- それ以外の状態で、残り転送数が3回連続(約150ms)変化しなければ、DMAを
  `dma_channel_abort()`で強制的に中断し、SPI0のRX FIFOに残っている可能性の
  ある半端なバイトを読み捨てた上で、ヘッダー待ち状態へリセットする。

CSピン(GP1)には弱プルアップ(`gpio_pull_up()`)も掛けており、送信側のGPIO
初期化タイミングでCSラインが一時的に浮くことによる誤動作を軽減している
(あくまで保険。本体の復旧はウォッチドッグが担う)。

## フォント展開(`draw_glyph()`)

`PKT_TEXT_CMDS`/`PKT_BEZEL_CMDS`のCMD_TEXTコマンドは文字コードのみを送り、
実際のグリフ展開は受信側で行う。使用しているフォントデータ
(`font_petme128_8x8.h`)はMicroPython本体の`extmod/font_petme128_8x8.h`
そのもの(MIT license, Copyright (c) 2013, 2014 Damien P. George)。

フォーマットは「1byte = 1列(8pixel分)、LSBが上端」の列優先ビットマップ
(MicroPythonの`framebuf.FrameBuffer.text()`と同一の解釈)。送信側で
`framebuf.FrameBuffer.text()`を使って一度ラスタライズしてから送るのでは
なく、文字コードだけを送ることで、送信側の負担(RAM・処理時間)を大幅に
減らしている。

## 実機検証済みの重要な制約(再掲)

- **`stdio_init_all()`(USB CDC)を使わない**: HSTXのリアルタイム性を壊し、
  HDMIモニタ接続直後からNO SIGNALになることが実機で判明した。デバッグ出力は
  画面左上16x16の目視マーカー(パケット受信完了ごとに赤/緑トグル)のみに
  頼っている。`hstx_irq_count`/`spi_irq_count`という診断用カウンタは
  コード上に残っているが、読み出す手段は用意していない。
- **SPIモード3必須**: [hardware_guide.md](hardware_guide.md)参照。
- **バス優先度**: `bus_ctrl_hw->priority`でDMAの読み書き優先度を上げている
  (`BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS`)。

## HSTX/DVI出力そのものについて

公式pico-examples(`raspberrypi/pico-examples`の
`hstx/dvi_out_hstx_encoder/dvi_out_hstx_encoder.c`, Copyright (c) 2024
Raspberry Pi (Trading) Ltd.)の640x480 RGB332出力コードをほぼそのまま
流用している。TMDS符号化・タイミング生成・ピンポンDMAによるスキャンアウトの
詳細はそちらのコード・公式ドキュメントを参照。このプロジェクト固有の変更点は
「読み出し元をSPI受信で埋まっていく`framebuf[]`に差し替えた」点のみ。
