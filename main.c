// HDMI受信ブリッジ (PICO-HDMI-PLUS + Pico 2 / RP2350) — 汎用版
//
// MSX_emu_pico2とPB-1000_emu_AG2のどちらの本体エミュレータからでも、この
// 同一ファームウェア(書き込み直し不要)で受信できるように設計されている。
// 送信側ごとに固定の解像度/拡大率/ビット深度をファームウェアに埋め込む代わりに、
// パケットヘッダーに毎フレーム自己記述させることで受信側を完全に汎用化した
// (旧版はMSX用/PB-1000用でそれぞれ別々にビルドする必要があった)。
//
// HSTXの部分は公式pico-examples (raspberrypi/pico-examples,
// hstx/dvi_out_hstx_encoder/dvi_out_hstx_encoder.c, Copyright (c) 2024
// Raspberry Pi (Trading) Ltd.) の640x480 RGB332出力コードをほぼそのまま流用。
//
// 配線 (本体側Pico2 SPI1 → HDMI側Pico2 SPI0スレーブ、既存のLCD/SD配線
// GP10/11は共有、GP28を新設CSに使用):
//   本体Pico2 GP10 (SCK)       -> HDMI側Pico2 GP2 (SCK)
//   本体Pico2 GP11 (MOSI)      -> HDMI側Pico2 GP0 (RX)
//   本体Pico2 GP28 (新設CS)    -> HDMI側Pico2 GP1 (CSn)
//   GND共通
//
// プロトコル: 1回のCS Low区間 = 1パケット = 8byteヘッダー + ペイロード。
//   ヘッダー[0] = パケット種別
//     0x00 = PKT_PALETTE    (パレット更新。ヘッダー[1]=エントリ数(0は256扱い)、
//            ペイロード=エントリ数byte、RGB332)
//     0x01 = PKT_FRAME       (ピクセルフレーム。それ以外/未知の種別も前方互換
//            のためPKT_FRAMEとして扱う)
//     0x02 = PKT_TEXT_CMDS   (文字コマンド列。EMULATOR MENUミラー用 —
//            mp/hdmi_menu_mirror.py参照。ピクセルではなく「矩形塗り」「文字列
//            描画」の2種類のコマンドを送り、受信側でフォント展開する。送信側
//            バッファが数百byte〜数KBで済み、PKT_FRAMEのように解像度に比例した
//            大きなバッファを本体Pico2側に持たずに済む)
//     0x03 = PKT_BEZEL_CMDS  (ベゼル描画用 — pb1000.py _draw_bezel_hdmi()。
//            ヘッダー/ペイロード書式はPKT_TEXT_CMDSと全く同じだが、ウィンドウ
//            中央寄せの最大サイズ追跡(KIND_*)をPKT_TEXT_CMDSとは独立させる
//            ための別種別 — 両方とも文字コマンド列だが、EMULATOR MENUは
//            ゲーム画面よりずっと大きいキャンバスを使うため、同じ追跡にすると
//            ゲーム画面や小さいベゼルのウィンドウがメニューの大きさに
//            引きずられて画面中央からずれてしまう)
//     0x04 = PKT_CLEAR_SCREEN (画面全体クリア用 — main.pyが起動の最初に送る。
//            本体Pico2の再起動直後、受信側Pico2は電源が入ったままなので前回
//            セッションの表示が残ってしまうため。ヘッダーのみ、ダミー1byteの
//            ペイロード。画面全体を黒にし、KIND_*の最大サイズ追跡もリセット
//            する — 通常のPKT_TEXT_CMDS等で大きい黒矩形を送る方式にしないのは、
//            それだとそのKIND自体の最大サイズが膨らんで後続フレームの
//            センタリングが狂ってしまうため)
//   PKT_FRAMEのヘッダー:
//     [1] = bpp (1/2/4/8。それ以外の値は8として扱う)
//           8bpp: 1byte/pixelがそのままRGB332(パレット参照なし)
//           1/2/4bpp: 1byteに(8/bpp)pixel分のパレット番号をMSB詰めで格納
//                     (例: 4bppなら上位nibble=先頭pixel、下位nibble=次pixel)。
//                     直近のPKT_PALETTEで受信した内容を参照する。
//     [2],[3] = width (uint16, big-endian)。0または上限超過はMAX_IMG_Wに丸める。
//     [4],[5] = height (uint16, big-endian)。0または上限超過はMAX_IMG_Hに丸める。
//     [6] = scale (整数倍拡大率。0は1として扱う。MAX_SCALEを超えたら丸める)
//     [7] = 予約(0)
//   ペイロード = width * height * bpp / 8 byte (行優先。1行 = width*bpp/8 byte、
//   端数が出ないようwidthは(8/bpp)の倍数であることが送信側の責務)。
//
//   PKT_TEXT_CMDSのヘッダー(bpp/予約の位置をpayload_lenの上位/下位byteに転用):
//     [1] = payload_len 上位byte
//     [2],[3] = width  (uint16, big-endian) — 論理キャンバス幅(センタリング用)
//     [4],[5] = height (uint16, big-endian) — 論理キャンバス高さ
//     [6] = scale
//     [7] = payload_len 下位byte
//   ペイロード = payload_len byte のコマンド列 (MAX_PAYLOADを超える場合は
//   丸められる — 実際に送るコマンド列がこれを超えないようにするのは送信側の
//   責務。1画面分は通常でも高々数百byte程度)。各コマンドは以下のいずれか:
//     CMD_RECT (0x00): [cmd][x_hi,x_lo][y_hi,y_lo][w_hi,w_lo][h_hi,h_lo][color]
//       = 10byte(cmd byte込み)。キャンバス座標(x,y)からw*h の矩形をcolor
//       (RGB332)で塗る。
//     CMD_TEXT (0x01): [cmd][x_hi,x_lo][y_hi,y_lo][fg][bg][str_len][文字コード...]
//       = 8+str_len byte(cmd byte込み)。文字コード(32-127、範囲外は127=市松模様)を
//       font_petme128_8x8[](MicroPython本体と同一フォント、MIT license)で
//       1文字ずつ8x8展開し、x方向へ8pixelずつ進める。
//   末尾までコマンドを順次処理する。未知のコマンドIDに遭遇したら安全側に
//   倒してそこで処理を打ち切る(以降のバイトはコマンド境界を見失っている
//   可能性があるため解釈しない)。
//
// 表示ウィンドウは「これまで受信した最大のwidth/height」を基準に画面中央へ
// 固定される(送信元の解像度は原則セッション中一定という前提)。フレームごとに
// 前回の表示領域を黒でクリアしてから今回のフレームを描くため、解像度が
// 縮んだ場合(例: PB-1000の64→32ドット切替)も塗り残しは発生しない。
//
// ホットスワップ対応: このPico2の電源を入れたまま、送信側(本体Pico2)を
// MSX用/PB-1000用の間で挿し替えても、受信側を再起動・再書き込みせずに
// そのまま追従する。パケット自体は上記の通り毎フレーム自己記述式なので
// 送信元の判別自体は問題ないが、挿し替えの瞬間にSPI受信がヘッダーまたは
// ペイロードの途中で止まってしまう(残りバイトを待ったままフリーズする)
// 問題があるため、hdmi_rx_watchdog_cb()が一定間隔でDMAの進捗を監視し、
// 進捗が止まっていれば強制的にヘッダー待ち状態へリセットする。

#include <string.h>
#include "pico/time.h"
#include "font_petme128_8x8.h"

// 切り分け用: 0にするとSPI0スレーブ受信を初期化せず、HSTXのDVI出力だけを行う。
#ifndef ENABLE_SPI_RX
#define ENABLE_SPI_RX 1
#endif

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/hstx_ctrl.h"
#include "hardware/structs/hstx_fifo.h"
#include "hardware/structs/sio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

// ----------------------------------------------------------------------------
// 画面ジオメトリ

#define SCREEN_W 640
#define SCREEN_H 480

// PKT_FRAME(ピクセル直送)側の送信元が使いうる最大サイズ(現状: MSXが
// 256x192、PB-1000本体画面が192x64)。ここを超える解像度をPKT_FRAMEで
// 送る新しい送信元が加わる場合は拡張すること — 超えた場合、width/height
// がここへ丸められてSPI受信バイト数が実際の送信バイト数と食い違い、
// パケットの同期がずれる(単なる表示崩れでは済まない)。
// PKT_TEXT_CMDS(EMULATOR MENUミラー)の論理キャンバスサイズはこことは
// 無関係 — payload_lenをヘッダーで明示するため、scratch_buf(MAX_PAYLOAD
// byte)に収まる範囲であれば実際の物理パネル解像度(480x320等)をそのまま
// 送ってよい。センタリング計算上の上限は画面サイズ(SCREEN_W/H)そのもの。
#define MAX_IMG_W 256
#define MAX_IMG_H 192
#define MAX_SCALE 4

#define MAX_PAYLOAD (MAX_IMG_W * MAX_IMG_H) // PKT_FRAME bpp=8のときが最大(1byte/pixel)。
                                             // PKT_TEXT_CMDSのpayload_lenもこの範囲に丸める
                                             // (scratch_bufを共用するため)。

static uint8_t framebuf[SCREEN_W * SCREEN_H]; // RGB332, 1byte/pixel
// SPI受信用のダブルバッファ。DMAが書き込み中でないほうをCore1がframebufへ
// コピーする(Core0のISRでmemcpyすると、HSTXの走査線再設定(約32us周期)を
// 圧迫してHDMI同期が崩れるため、コピーはCore1へオフロードする)。
static uint8_t scratch_buf[2][MAX_PAYLOAD];

// ----------------------------------------------------------------------------
// DVI (HSTX) 定数 — 公式サンプルと同一 (640x480 @ 60Hz)

#define TMDS_CTRL_00 0x354u
#define TMDS_CTRL_01 0x0abu
#define TMDS_CTRL_10 0x154u
#define TMDS_CTRL_11 0x2abu

#define SYNC_V0_H0 (TMDS_CTRL_00 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V0_H1 (TMDS_CTRL_01 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H0 (TMDS_CTRL_10 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))
#define SYNC_V1_H1 (TMDS_CTRL_11 | (TMDS_CTRL_00 << 10) | (TMDS_CTRL_00 << 20))

#define MODE_H_SYNC_POLARITY 0
#define MODE_H_FRONT_PORCH   16
#define MODE_H_SYNC_WIDTH    96
#define MODE_H_BACK_PORCH    48
#define MODE_H_ACTIVE_PIXELS SCREEN_W

#define MODE_V_SYNC_POLARITY 0
#define MODE_V_FRONT_PORCH   10
#define MODE_V_SYNC_WIDTH    2
#define MODE_V_BACK_PORCH    33
#define MODE_V_ACTIVE_LINES  SCREEN_H

#define MODE_H_TOTAL_PIXELS ( \
    MODE_H_FRONT_PORCH + MODE_H_SYNC_WIDTH + \
    MODE_H_BACK_PORCH  + MODE_H_ACTIVE_PIXELS \
)
#define MODE_V_TOTAL_LINES  ( \
    MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + \
    MODE_V_BACK_PORCH  + MODE_V_ACTIVE_LINES \
)

#define HSTX_CMD_RAW         (0x0u << 12)
#define HSTX_CMD_RAW_REPEAT  (0x1u << 12)
#define HSTX_CMD_TMDS        (0x2u << 12)
#define HSTX_CMD_TMDS_REPEAT (0x3u << 12)
#define HSTX_CMD_NOP         (0xfu << 12)

static uint32_t vblank_line_vsync_off[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V1_H1,
    HSTX_CMD_NOP
};

static uint32_t vblank_line_vsync_on[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V0_H1,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V0_H0,
    HSTX_CMD_RAW_REPEAT | (MODE_H_BACK_PORCH + MODE_H_ACTIVE_PIXELS),
    SYNC_V0_H1,
    HSTX_CMD_NOP
};

static uint32_t vactive_line[] = {
    HSTX_CMD_RAW_REPEAT | MODE_H_FRONT_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_SYNC_WIDTH,
    SYNC_V1_H0,
    HSTX_CMD_NOP,
    HSTX_CMD_RAW_REPEAT | MODE_H_BACK_PORCH,
    SYNC_V1_H1,
    HSTX_CMD_TMDS       | MODE_H_ACTIVE_PIXELS
};

// ----------------------------------------------------------------------------
// HSTX scanout DMA (公式サンプルと同一ロジック、read元をframebuf[]に変更)

#define DMACH_PING 0
#define DMACH_PONG 1

static bool dma_pong = false;
static uint v_scanline = 2;

// 診断用: HSTX/SPI受信それぞれの割り込みが実際に発生し続けているかを追跡する
// カウンタ(USBシリアルは実機でHSTXのリアルタイム性を壊すため、通常運用では
// 読み出す手段は目視マーカーのみ)。
static volatile uint32_t hstx_irq_count = 0;
static volatile uint32_t spi_irq_count = 0;

void __scratch_x("") hstx_dma_irq_handler(void) {
    hstx_irq_count++;
    uint ch_num = dma_pong ? DMACH_PONG : DMACH_PING;
    dma_channel_hw_t *ch = &dma_hw->ch[ch_num];
    dma_hw->intr = 1u << ch_num;
    dma_pong = !dma_pong;

    static bool vactive_cmdlist_posted = false;

    if (v_scanline >= MODE_V_FRONT_PORCH && v_scanline < (MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH)) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_on;
        ch->transfer_count = count_of(vblank_line_vsync_on);
    } else if (v_scanline < MODE_V_FRONT_PORCH + MODE_V_SYNC_WIDTH + MODE_V_BACK_PORCH) {
        ch->read_addr = (uintptr_t)vblank_line_vsync_off;
        ch->transfer_count = count_of(vblank_line_vsync_off);
    } else if (!vactive_cmdlist_posted) {
        ch->read_addr = (uintptr_t)vactive_line;
        ch->transfer_count = count_of(vactive_line);
        vactive_cmdlist_posted = true;
    } else {
        ch->read_addr = (uintptr_t)&framebuf[(v_scanline - (MODE_V_TOTAL_LINES - MODE_V_ACTIVE_LINES)) * MODE_H_ACTIVE_PIXELS];
        ch->transfer_count = MODE_H_ACTIVE_PIXELS / sizeof(uint32_t);
        vactive_cmdlist_posted = false;
    }

    if (!vactive_cmdlist_posted) {
        v_scanline = (v_scanline + 1) % MODE_V_TOTAL_LINES;
    }
}

static void hstx_init(void) {
    // RGB332 (8bit/pixel) 展開設定。公式サンプルと同一。
    hstx_ctrl_hw->expand_tmds =
        2  << HSTX_CTRL_EXPAND_TMDS_L2_NBITS_LSB |
        0  << HSTX_CTRL_EXPAND_TMDS_L2_ROT_LSB   |
        2  << HSTX_CTRL_EXPAND_TMDS_L1_NBITS_LSB |
        29 << HSTX_CTRL_EXPAND_TMDS_L1_ROT_LSB   |
        1  << HSTX_CTRL_EXPAND_TMDS_L0_NBITS_LSB |
        26 << HSTX_CTRL_EXPAND_TMDS_L0_ROT_LSB;

    hstx_ctrl_hw->expand_shift =
        4 << HSTX_CTRL_EXPAND_SHIFT_ENC_N_SHIFTS_LSB |
        8 << HSTX_CTRL_EXPAND_SHIFT_ENC_SHIFT_LSB |
        1 << HSTX_CTRL_EXPAND_SHIFT_RAW_N_SHIFTS_LSB |
        0 << HSTX_CTRL_EXPAND_SHIFT_RAW_SHIFT_LSB;

    hstx_ctrl_hw->csr = 0;
    hstx_ctrl_hw->csr =
        HSTX_CTRL_CSR_EXPAND_EN_BITS |
        5u << HSTX_CTRL_CSR_CLKDIV_LSB |
        5u << HSTX_CTRL_CSR_N_SHIFTS_LSB |
        2u << HSTX_CTRL_CSR_SHIFT_LSB |
        HSTX_CTRL_CSR_EN_BITS;

    // HSTX出力0-7はGPIO12-19に現れる (PICO-HDMI-PLUS / Pico-DVI-Sock配線):
    //   GP12 D0+  GP13 D0-
    //   GP14 CK+  GP15 CK-
    //   GP16 D2+  GP17 D2-
    //   GP18 D1+  GP19 D1-
    hstx_ctrl_hw->bit[2] = HSTX_CTRL_BIT0_CLK_BITS;
    hstx_ctrl_hw->bit[3] = HSTX_CTRL_BIT0_CLK_BITS | HSTX_CTRL_BIT0_INV_BITS;
    for (uint lane = 0; lane < 3; ++lane) {
        static const int lane_to_output_bit[3] = {0, 6, 4};
        int bit = lane_to_output_bit[lane];
        uint32_t lane_data_sel_bits =
            (lane * 10    ) << HSTX_CTRL_BIT0_SEL_P_LSB |
            (lane * 10 + 1) << HSTX_CTRL_BIT0_SEL_N_LSB;
        hstx_ctrl_hw->bit[bit    ] = lane_data_sel_bits;
        hstx_ctrl_hw->bit[bit + 1] = lane_data_sel_bits | HSTX_CTRL_BIT0_INV_BITS;
    }

    for (int i = 12; i <= 19; ++i) {
        gpio_set_function(i, 0); // HSTX
    }

    dma_channel_config c;
    c = dma_channel_get_default_config(DMACH_PING);
    channel_config_set_chain_to(&c, DMACH_PONG);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        DMACH_PING, &c, &hstx_fifo_hw->fifo,
        vblank_line_vsync_off, count_of(vblank_line_vsync_off), false
    );
    c = dma_channel_get_default_config(DMACH_PONG);
    channel_config_set_chain_to(&c, DMACH_PING);
    channel_config_set_dreq(&c, DREQ_HSTX);
    dma_channel_configure(
        DMACH_PONG, &c, &hstx_fifo_hw->fifo,
        vblank_line_vsync_off, count_of(vblank_line_vsync_off), false
    );

    dma_hw->ints0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    dma_hw->inte0 = (1u << DMACH_PING) | (1u << DMACH_PONG);
    irq_set_exclusive_handler(DMA_IRQ_0, hstx_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_channel_start(DMACH_PING);
}

// ----------------------------------------------------------------------------
// SPI0スレーブ受信 (本体Pico2 <- SPI1マスター、GP0=RX GP1=CSn GP2=SCK)
//
// 2段階受信: まず8byteヘッダーを受信し、その内容(種別/bpp/width/height)から
// 本体ペイロードの受信バイト数を決めて再度DMAを組む。

#define SPI_RX_DMA_CHAN 2

#define PKT_PALETTE      0x00u
#define PKT_FRAME        0x01u
#define PKT_TEXT_CMDS    0x02u // EMULATOR MENUミラー(mp/hdmi_menu_mirror.py)
#define PKT_BEZEL_CMDS   0x03u // ベゼル描画(pb1000.py _draw_bezel_hdmi())。
                                // コマンド書式はPKT_TEXT_CMDSと全く同じだが、
                                // ウィンドウ中央寄せの最大サイズ追跡を独立させる
                                // ため別種別にしている — 詳細はKIND_*の説明参照。
#define PKT_CLEAR_SCREEN 0x04u // 画面全体を黒でクリアし、KIND_*の追跡状態も
                                // 全てリセットする(ヘッダーのみ、ダミー1byteの
                                // ペイロード)。本体Pico2の再起動直後、受信側は
                                // 電源が入ったままなので前回セッションの表示が
                                // 残ってしまう — main.pyが起動の最初(HDMI初期化
                                // 直後)に送る。中央寄せ用の最大サイズ追跡等には
                                // 一切影響を与えない専用パケットとして分離して
                                // いる(通常のPKT_TEXT_CMDS等で640x480の黒矩形を
                                // 送る方式だと、そのKIND自体の最大サイズが640x480
                                // に膨らんでしまい、後続の本来小さいフレームの
                                // センタリングが狂う — 過去に修正した不具合と
                                // 同じ原因になるため避けている)。

// KIND_*: buf_meta[].kindの値。ウィンドウ中央寄せ(core1_copy_loop()の
// max_w[]/max_h[])をこの3種別で完全に分離して追跡する。
//   KIND_PIXEL: ゲーム画面(PKT_FRAME、例: 192x64)
//   KIND_TEXT:  EMULATOR MENU(PKT_TEXT_CMDS、例: 480x320 — ゲーム画面より
//               大幅に大きい)
//   KIND_BEZEL: ベゼル(PKT_BEZEL_CMDS)。ゲーム画面と同じ論理座標系
//               (192 x lcd_height、送信側と同じ固定scale)で送られてくる
//               前提で、ゲーム画面のウィンドウを対称に一回り大きく包む
//               形で中央寄せされ、視覚的に画面を縁取るベゼルとして重なる。
// もしPKT_TEXT_CMDSとPKT_BEZEL_CMDSを同じ種別として追跡すると、メニュー
// (大きい)を一度表示しただけでベゼル(小さい)側のウィンドウがメニューの
// 大きさに引きずられてしまう — ゲーム画面が一度でも表示された後にメニュー
// が表示されると左上に寄ってしまったのと全く同じ種類の不具合になるため、
// 独立させている。
#define KIND_PIXEL 0
#define KIND_TEXT  1
#define KIND_BEZEL 2

#define MAX_PALETTE_ENTRIES 256
static uint8_t palette_staging[MAX_PALETTE_ENTRIES]; // DMA受信用(Core0からは書き込み専用)
static uint8_t palette_rgb332[MAX_PALETTE_ENTRIES];  // Core1が実際の展開に使う確定版

static uint8_t header_buf[8];

// Core0がヘッダー解析時に組み立て、ペイロード受信完了時にbuf_meta[]へ確定
// コピーする(pending_metaはCore0内でしか触らないのでvolatile不要)。
typedef struct {
    uint8_t  kind;          // KIND_PIXEL/KIND_TEXT/KIND_BEZEL
    uint8_t  bpp;           // kind==KIND_PIXELの場合のみ有効(1/2/4/8)
    uint16_t width;
    uint16_t height;
    uint8_t  scale;
    uint16_t payload_len;   // 実際にDMAで受信するペイロードのbyte数
} frame_meta_t;

static frame_meta_t pending_meta;
// Core1への通知(SDK標準のコア間FIFO)。フレーム受信完了時はバッファ番号
// (0/1)そのものを送るだけ — 中身の解釈に必要な情報は全てbuf_meta[]に
// (Core0が同じくFIFO pushの直前に書き込んでから)入れておく。SIOのFIFO
// push/popはCore間の順序保証を伴う操作のため、これで十分。
static volatile frame_meta_t buf_meta[2];
#define FIFO_MSG_PALETTE      0x80000000u
#define FIFO_MSG_CLEAR_SCREEN 0x40000000u

static volatile int spi_rx_active_buf = 0;   // フレームDMAが現在書き込み中のバッファ
static volatile bool waiting_for_header = true;
static volatile bool pending_is_palette = false;
static volatile bool pending_is_clear = false;

static void spi_rx_dma_irq_handler(void) {
    dma_hw->ints1 = 1u << SPI_RX_DMA_CHAN;
    spi_irq_count++;

    // Core0のこのISRは、DMAの再アームとCore1への通知(FIFO push)以外の処理を
    // 一切行わない。memcpy等の重い処理は絶対に入れないこと — 実機で、ISR内に
    // 小さな描画処理が残っているだけでも、HSTXの走査線再設定(約32us周期)を
    // 妨げ、数分〜数十分単位でHDMI信号を失う不具合があった
    // (doc/hdmi_bridge_phase2_report.md参照)。

    if (waiting_for_header) {
        uint8_t pkt_type = header_buf[0];
        uint8_t *dst;
        uint32_t len;

        if (pkt_type == PKT_PALETTE) {
            uint32_t entries = header_buf[1];
            if (entries == 0) entries = MAX_PALETTE_ENTRIES;
            if (entries > MAX_PALETTE_ENTRIES) entries = MAX_PALETTE_ENTRIES;
            pending_is_palette = true;
            pending_is_clear = false;
            len = entries;
            dst = palette_staging;
        } else if (pkt_type == PKT_CLEAR_SCREEN) {
            // ダミー1byteのペイロードのみ。中身は使わないのでpalette_staging
            // を一時的な受け皿として使い回す(専用バッファは用意しない)。
            pending_is_palette = false;
            pending_is_clear = true;
            len = 1;
            dst = palette_staging;
        } else if (pkt_type == PKT_TEXT_CMDS || pkt_type == PKT_BEZEL_CMDS) {
            uint32_t payload_len = (((uint32_t)header_buf[1]) << 8) | header_buf[7];
            if (payload_len > MAX_PAYLOAD) payload_len = MAX_PAYLOAD;

            uint32_t width  = ((uint32_t)header_buf[2] << 8) | header_buf[3];
            uint32_t height = ((uint32_t)header_buf[4] << 8) | header_buf[5];
            if (width == 0) width = 1;
            if (width > SCREEN_W) width = SCREEN_W;
            if (height == 0) height = 1;
            if (height > SCREEN_H) height = SCREEN_H;

            uint32_t scale = header_buf[6];
            if (scale == 0) scale = 1;
            if (scale > MAX_SCALE) scale = MAX_SCALE;
            while (scale > 1 && (width * scale > SCREEN_W || height * scale > SCREEN_H)) {
                scale--;
            }

            pending_is_palette = false;
            pending_is_clear = false;
            pending_meta.kind = (pkt_type == PKT_BEZEL_CMDS) ? KIND_BEZEL : KIND_TEXT;
            pending_meta.bpp = 0;
            pending_meta.width = (uint16_t)width;
            pending_meta.height = (uint16_t)height;
            pending_meta.scale = (uint8_t)scale;
            pending_meta.payload_len = (uint16_t)payload_len;

            len = payload_len;
            dst = scratch_buf[spi_rx_active_buf];
        } else {
            // PKT_FRAME、および未知の種別も前方互換のためPKT_FRAMEとして扱う。
            uint8_t bpp = header_buf[1];
            if (bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) bpp = 8;

            uint32_t width  = ((uint32_t)header_buf[2] << 8) | header_buf[3];
            uint32_t height = ((uint32_t)header_buf[4] << 8) | header_buf[5];
            if (width == 0 || width > MAX_IMG_W) width = MAX_IMG_W;
            if (height == 0 || height > MAX_IMG_H) height = MAX_IMG_H;

            uint32_t scale = header_buf[6];
            if (scale == 0) scale = 1;
            if (scale > MAX_SCALE) scale = MAX_SCALE;
            // このフレーム自身のwidth/height*scaleが画面に収まることを保証する
            // (Core1側のscaled_row/framebufの書き込みが範囲外に出ないための
            // 安全策。実際の送信側は常にこの範囲に収まる値を送ってくる)。
            while (scale > 1 && (width * scale > SCREEN_W || height * scale > SCREEN_H)) {
                scale--;
            }

            pending_is_palette = false;
            pending_is_clear = false;
            pending_meta.kind = KIND_PIXEL;
            pending_meta.bpp = bpp;
            pending_meta.width = (uint16_t)width;
            pending_meta.height = (uint16_t)height;
            pending_meta.scale = (uint8_t)scale;

            len = (width * height * bpp) / 8u;
            if (len > MAX_PAYLOAD) len = MAX_PAYLOAD;
            pending_meta.payload_len = (uint16_t)len;
            dst = scratch_buf[spi_rx_active_buf];
        }

        waiting_for_header = false;
        dma_channel_set_write_addr(SPI_RX_DMA_CHAN, dst, false);
        dma_channel_set_trans_count(SPI_RX_DMA_CHAN, len, true);
    } else {
        // ペイロード受信完了。Core1へ通知してから、次のヘッダー受信へ戻る。
        if (pending_is_palette) {
            multicore_fifo_push_blocking(FIFO_MSG_PALETTE);
        } else if (pending_is_clear) {
            multicore_fifo_push_blocking(FIFO_MSG_CLEAR_SCREEN);
        } else {
            int filled = spi_rx_active_buf;
            buf_meta[filled] = pending_meta;
            spi_rx_active_buf = 1 - filled;
            multicore_fifo_push_blocking((uint32_t)filled);
        }

        waiting_for_header = true;
        dma_channel_set_write_addr(SPI_RX_DMA_CHAN, header_buf, false);
        dma_channel_set_trans_count(SPI_RX_DMA_CHAN, sizeof(header_buf), true);
    }
}

// ----------------------------------------------------------------------------
// ホットスワップ監視 (送信側Pico2の挿し替え検出・復旧)
//
// 送信側を電源を切らずに挿し替えると、ちょうどヘッダーやペイロードの途中で
// SPIクロックが止まることがある。DMAは「あとN byte来たら完了」という形で
// 待っているだけなので、その続きが物理的に来なくなると完了割り込みが永久に
// 発生せず、spi_rx_dma_irq_handler()による自己修復(次のヘッダー待ちへ戻る)が
// 効かないまま止まってしまう。これを検出するため、一定間隔でDMAの残り
// 転送数を監視し、「ヘッダー待ちで1byteも受信していない(=何も繋がっていない
// か、次のパケットを待っているだけの正常なアイドル状態)」以外の状態で
// 複数回連続して進捗が無ければ、DMAを強制的に中断してヘッダー待ち状態へ
// リセットする。これにより、新しく挿した送信側の次のパケットから正しく
// 同期を取り直せる。

#define WATCHDOG_INTERVAL_MS 50
#define WATCHDOG_STALL_TICKS 3 // 連続何回(=約150ms)進捗がなければリセットするか

static uint32_t watchdog_last_remaining = 0xFFFFFFFFu;
static int watchdog_stall_ticks = 0;

static bool hdmi_rx_watchdog_cb(repeating_timer_t *rt) {
    (void)rt;
    uint32_t remaining = dma_hw->ch[SPI_RX_DMA_CHAN].transfer_count;

    // ヘッダーの1byte目も受信していない状態は、送信側が単に何も送っていない
    // だけの正常なアイドル状態なので、いつまで続いても異常とはみなさない。
    if (waiting_for_header && remaining == sizeof(header_buf)) {
        watchdog_stall_ticks = 0;
        watchdog_last_remaining = remaining;
        return true;
    }

    if (remaining == watchdog_last_remaining) {
        watchdog_stall_ticks++;
        if (watchdog_stall_ticks >= WATCHDOG_STALL_TICKS) {
            // Core0のDMA完了割り込み(spi_rx_dma_irq_handler)と競合しないよう、
            // 割り込みを止めた区間でDMAの中断・状態リセットを行う。
            uint32_t save = save_and_disable_interrupts();
            dma_channel_abort(SPI_RX_DMA_CHAN);
            // PL022のRX FIFOに残っている可能性のある半端なバイトを読み捨てる。
            while (spi_is_readable(spi0)) {
                (void)spi_get_hw(spi0)->dr;
            }
            waiting_for_header = true;
            pending_is_palette = false;
            dma_channel_set_write_addr(SPI_RX_DMA_CHAN, header_buf, false);
            dma_channel_set_trans_count(SPI_RX_DMA_CHAN, sizeof(header_buf), true);
            restore_interrupts(save);

            watchdog_stall_ticks = 0;
            watchdog_last_remaining = sizeof(header_buf);
        }
    } else {
        watchdog_stall_ticks = 0;
        watchdog_last_remaining = remaining;
    }
    return true;
}

// 受信できているかを目視確認するためのマーカー。画面左上16x16に、1フレーム
// 受信完了ごとに交互の色を出す。これが変化しなければSPI受信そのものが
// 発生していないことになる。
#define MARKER_SIZE 16
static void draw_marker(uint8_t color) {
    for (int row = 0; row < MARKER_SIZE; row++) {
        memset(&framebuf[row * SCREEN_W], color, MARKER_SIZE);
    }
}

// framebuf上の絶対ピクセル座標(x0,y0)-(x0+w,y0+h)を単色で塗る(画面外は
// クリップ)。PKT_TEXT_CMDSのCMD_RECT用。
static void fill_px_rect(int x0, int y0, int w, int h, uint8_t color) {
    int x1 = x0 + w, y1 = y0 + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    if (x1 <= x0 || y1 <= y0) return;
    int rw = x1 - x0;
    for (int row = y0; row < y1; row++) {
        memset(&framebuf[row * SCREEN_W + x0], color, rw);
    }
}

// 1文字をframebuf上の絶対ピクセル座標(px,py)へ8x8フォント(font_petme128_8x8,
// 1byte=1列・LSBが上端)からscale倍で展開する。範囲外の文字コードは
// MicroPython本体のframebuf.text()と同じくコード127(市松模様)にフォール
// バックする。
static void draw_glyph(int px, int py, uint8_t ch, uint8_t fg, uint8_t bg, int scale) {
    if (ch < 32 || ch > 127) ch = 127;
    const uint8_t *col_data = &font_petme128_8x8[(ch - 32) * 8];
    for (int c = 0; c < 8; c++) {
        uint8_t colbits = col_data[c];
        int cx = px + c * scale;
        if (cx + scale <= 0 || cx >= SCREEN_W) continue;
        for (int r = 0; r < 8; r++) {
            uint8_t color = (colbits & (1u << r)) ? fg : bg;
            int cy = py + r * scale;
            if (cy + scale <= 0 || cy >= SCREEN_H) continue;
            for (int sy = 0; sy < scale; sy++) {
                int fy = cy + sy;
                if (fy < 0 || fy >= SCREEN_H) continue;
                uint8_t *row = &framebuf[fy * SCREEN_W];
                for (int sx = 0; sx < scale; sx++) {
                    int fx = cx + sx;
                    if (fx < 0 || fx >= SCREEN_W) continue;
                    row[fx] = color;
                }
            }
        }
    }
}

#define CMD_RECT 0x00u
#define CMD_TEXT 0x01u

// PKT_TEXT_CMDSのペイロード(可変長コマンド列)を解釈してframebufへ描画する。
// left_margin/top_marginはウィンドウの左上(論理座標(0,0)に対応する絶対
// ピクセル位置)、scaleは論理→物理ピクセルの拡大率。
static void render_text_cmds(const uint8_t *buf, int len, int scale,
                              int left_margin, int top_margin) {
    int pos = 0;
    while (pos < len) {
        uint8_t cmd = buf[pos++];
        if (cmd == CMD_RECT) {
            if (pos + 9 > len) break;
            int x = ((int)buf[pos] << 8) | buf[pos + 1]; pos += 2;
            int y = ((int)buf[pos] << 8) | buf[pos + 1]; pos += 2;
            int w = ((int)buf[pos] << 8) | buf[pos + 1]; pos += 2;
            int h = ((int)buf[pos] << 8) | buf[pos + 1]; pos += 2;
            uint8_t color = buf[pos++];
            fill_px_rect(left_margin + x * scale, top_margin + y * scale,
                         w * scale, h * scale, color);
        } else if (cmd == CMD_TEXT) {
            // x(2)+y(2)+fg(1)+bg(1)+str_len(1) = 7byte。str_len自体を読む前に
            // 7byte分の余裕を確認する(以前は6byteしか確認しておらず、
            // payload境界ぎりぎりでstr_lenを範囲外読みする可能性があった)。
            if (pos + 7 > len) break;
            int x = ((int)buf[pos] << 8) | buf[pos + 1]; pos += 2;
            int y = ((int)buf[pos] << 8) | buf[pos + 1]; pos += 2;
            uint8_t fg = buf[pos++];
            uint8_t bg = buf[pos++];
            uint8_t slen = buf[pos++];
            if (pos + slen > len) break;
            int gx = left_margin + x * scale;
            int gy = top_margin + y * scale;
            for (int i = 0; i < slen; i++) {
                draw_glyph(gx, gy, buf[pos + i], fg, bg, scale);
                gx += 8 * scale;
            }
            pos += slen;
        } else {
            // 未知のコマンドID: コマンド境界を見失っている可能性があるため、
            // 安全側に倒してここで処理を打ち切る。
            break;
        }
    }
}

static void __not_in_flash_func(core1_copy_loop)(void) {
    static uint8_t scaled_row[MAX_IMG_W * MAX_SCALE];

    // 種別(KIND_PIXEL/KIND_TEXT/KIND_BEZEL)ごとに別々の最大サイズを追跡する
    // — KIND_*の定義コメント参照。ゲーム画面(小さい)とEMULATOR MENU
    // ミラー(大きい)を単一のmax_w/hで追跡すると、一度でも大きい方(メニュー)
    // が来た後はその最大値が残り続け、以後ゲーム画面側のウィンドウが誤った
    // サイズで計算されて画面左上に寄ってしまう不具合があったため、完全に
    // 分離している。ベゼルはゲーム画面と意図的に同じ論理座標系・拡大率で
    // 送られてくる(独立した別種別として追跡されつつ、結果的に画面上では
    // ゲーム画面のウィンドウを対称に一回り包む位置に中央寄せされる)。
    int max_w[3] = {0, 0, 0}, max_h[3] = {0, 0, 0};
    // 直前に描画した領域も種別ごとに独立して覚えておく(単一の共有変数だと、
    // 例えばゲーム画面(小)とベゼル(それを一回り包む、少し大きい)が交互に
    // 送られてくる場合に、ゲーム画面フレームの到着のたびに「前回領域(=直前の
    // ベゼルの領域)と違う」と判定されてベゼルの縁が毎回黒でクリアされてしまい、
    // ゲーム画面が動き出す(=ベゼルより高頻度でフレームが来る)と同時にベゼルが
    // 消えて見える不具合があった。ベゼル・ゲーム画面・メニューは互いに重なって
    // 共存しうる別々のレイヤーとして扱い、各レイヤーは自分自身の直前の領域
    // だけをクリアする。
    int last_left[3] = {-1, -1, -1}, last_top[3] = {-1, -1, -1};
    int last_ww[3] = {0, 0, 0}, last_wh[3] = {0, 0, 0};
    bool marker_toggle = false;

    while (1) {
        uint32_t msg = multicore_fifo_pop_blocking();

        if (msg == FIFO_MSG_PALETTE) {
            memcpy(palette_rgb332, palette_staging, sizeof(palette_rgb332));
            continue;
        }

        if (msg == FIFO_MSG_CLEAR_SCREEN) {
            // 本体Pico2の再起動直後などに送られてくる。画面全体を黒にし、
            // 全種別の追跡状態もリセットして、次に来るフレームから正しく
            // 再センタリングされるようにする。
            memset(framebuf, 0x00, sizeof(framebuf));
            for (int k = 0; k < 3; k++) {
                max_w[k] = 0; max_h[k] = 0;
                last_left[k] = -1; last_top[k] = -1;
                last_ww[k] = 0; last_wh[k] = 0;
            }
            continue;
        }

        int buf_idx = (int)msg;
        frame_meta_t meta = buf_meta[buf_idx]; // volatileから安定した値へコピー

        int width  = meta.width  < 1 ? 1 : meta.width;
        int height = meta.height < 1 ? 1 : meta.height;
        int scale  = meta.scale  < 1 ? 1 : meta.scale;
        int kind   = meta.kind;

        // 表示ウィンドウは「この種別についてこれまで受信した最大サイズ」を
        // 基準に画面中央へ固定する。送信元の解像度は原則セッション中一定
        // という前提だが、PB-1000の32<->64ドット切替のように縮む場合も
        // あるため、最大値の方だけを追跡し、最大値が更新された時だけ
        // 再センタリングする。
        if (width  > max_w[kind]) max_w[kind] = width;
        if (height > max_h[kind]) max_h[kind] = height;

        int win_w = max_w[kind] * scale;
        int win_h = max_h[kind] * scale;
        if (win_w > SCREEN_W) win_w = SCREEN_W;
        if (win_h > SCREEN_H) win_h = SCREEN_H;
        int left_margin = (SCREEN_W - win_w) / 2;
        int top_margin  = (SCREEN_H - win_h) / 2;

        // 2026-09: 受信確認用マーカーは、実際のゲーム画面(KIND_PIXEL、
        // bpp=4のパレット方式)の間はもう表示しない — 起動直後のROM選択
        // メニューやランタイムメニュー(MenuCanvas、bpp=8のRAW332方式)は
        // 引き続き交互の色でトグルし、それ以外(ゲームプレイ中)は左上を
        // 黒で塗り続けて隠す。プロトコル変更なしで、既存ヘッダーのbppを
        // 「メニュー画面かどうか」の判定に流用しているだけ — 送信側
        // (msx_render_to_hdmi()=bpp4はゲーム専用、msx_render_to_hdmi_
        // raw332()=bpp8はメニュー専用、どちらもmsx_core.c参照)は元々
        // この使い分けをしていたので変更不要。
        if (kind == KIND_PIXEL && meta.bpp == 8) {
            marker_toggle = !marker_toggle;
            draw_marker(marker_toggle ? 0xE0 /* 赤 */ : 0x1C /* 緑 */);
        } else {
            draw_marker(0x00); // 黒で隠す(ゲームプレイ中は毎フレーム上書き)
        }

        int cur_ww = width * scale;
        int cur_wh = height * scale;

        // ウィンドウの位置/サイズが「この種別について」前回と変わっていれば
        // (最大サイズ更新による再センタリング、または解像度が縮んだ場合)、
        // この種別の前回の表示領域をまず黒でクリアしてから今回のフレームを
        // 描く。差分計算はせず毎回「前回領域→黒」を行うことで、塗り残しを
        // 確実に防ぐ。他の種別(例: ベゼルから見たゲーム画面)の領域には
        // 触れない — 互いに重なって共存するレイヤーのため。
        if (last_left[kind] >= 0 && (last_left[kind] != left_margin || last_top[kind] != top_margin ||
                                      last_ww[kind] != cur_ww || last_wh[kind] != cur_wh)) {
            for (int row = 0; row < last_wh[kind]; row++) {
                memset(&framebuf[(last_top[kind] + row) * SCREEN_W + last_left[kind]], 0x00, last_ww[kind]);
            }
        }

        const uint8_t *src = scratch_buf[buf_idx];

        if (meta.kind != KIND_PIXEL) {
            // KIND_TEXT/KIND_BEZELはどちらも同じコマンド書式(CMD_RECT/CMD_TEXT)。
            render_text_cmds(src, meta.payload_len, scale, left_margin, top_margin);
        } else {
            uint8_t bpp = meta.bpp;
            int bytes_per_row = (width * bpp) / 8;

            for (int row = 0; row < height; row++) {
                const uint8_t *src_row = &src[row * bytes_per_row];
                if (bpp == 8) {
                    for (int col = 0; col < width; col++) {
                        uint8_t v = src_row[col];
                        uint8_t *sp = &scaled_row[col * scale];
                        for (int sx = 0; sx < scale; sx++) sp[sx] = v;
                    }
                } else {
                    int ppb = 8 / bpp; // 1byteあたりのpixel数
                    uint8_t mask = (uint8_t)((1u << bpp) - 1u);
                    for (int col = 0; col < width; col++) {
                        int byte_i = col / ppb;
                        int idx_in_byte = col % ppb;
                        int shift = 8 - (idx_in_byte + 1) * bpp;
                        uint8_t pidx = (src_row[byte_i] >> shift) & mask;
                        uint8_t v = palette_rgb332[pidx];
                        uint8_t *sp = &scaled_row[col * scale];
                        for (int sx = 0; sx < scale; sx++) sp[sx] = v;
                    }
                }
                uint8_t *dst_base = &framebuf[(top_margin + row * scale) * SCREEN_W + left_margin];
                for (int sy = 0; sy < scale; sy++) {
                    memcpy(dst_base + sy * SCREEN_W, scaled_row, cur_ww);
                }
            }
        }

        last_left[kind] = left_margin;
        last_top[kind]  = top_margin;
        last_ww[kind]   = cur_ww;
        last_wh[kind]   = cur_wh;
    }
}

static void spi_slave_init(void) {
    spi_init(spi0, 30 * 1000 * 1000);
    spi_set_slave(spi0, true);
    // RP2350のPL022 SPIスレーブ実装には、CPOL=0/CPHA=0(モード0)だと数バイト受信後に
    // 同期が崩れる既知の制約がある(Raspberry Pi公式フォーラムで複数報告あり)。
    // CPOL=1/CPHA=1(モード3)の方が安定するとの報告があるため採用
    // (MSX_emu_pico2の実機検証で確認済み)。
    spi_set_format(spi0, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    gpio_set_function(0, GPIO_FUNC_SPI); // RX  (master TX/MOSI相当)
    gpio_set_function(1, GPIO_FUNC_SPI); // CSn
    gpio_set_function(2, GPIO_FUNC_SPI); // SCK
    // GP3 (TX) は未使用(受信専用リンクのため配線しない)
    // CSnに弱プルアップを掛けておく — 送信側Pico2を挿し替えて配線が一時的に
    // 浮いた際、CSがフロートして意図しないクロックノイズを誤ってヘッダーの
    // 一部として拾ってしまう可能性を減らす(あくまで保険。本体の復旧は
    // hdmi_rx_watchdog_cb()が担う)。
    gpio_pull_up(1);

    dma_channel_config c = dma_channel_get_default_config(SPI_RX_DMA_CHAN);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, spi_get_dreq(spi0, false));
    // 最初はヘッダーを受信する状態から開始する。
    dma_channel_configure(
        SPI_RX_DMA_CHAN, &c,
        header_buf, &spi_get_hw(spi0)->dr,
        sizeof(header_buf), false
    );

    dma_hw->ints1 = 1u << SPI_RX_DMA_CHAN;
    dma_hw->inte1 = 1u << SPI_RX_DMA_CHAN;
    irq_set_exclusive_handler(DMA_IRQ_1, spi_rx_dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    dma_channel_start(SPI_RX_DMA_CHAN);
}

// ----------------------------------------------------------------------------

int main(void) {
    memset(framebuf, 0x00, sizeof(framebuf)); // 黒背景(周囲の余白は以後上書きしない)
#if ENABLE_SPI_RX
    memset(scratch_buf, 0x00, sizeof(scratch_buf));
    memset(palette_staging, 0x00, sizeof(palette_staging));
    memset(palette_rgb332, 0x00, sizeof(palette_rgb332));
    draw_marker(0x03); // 青 = 起動直後、まだ1フレームも受信していない状態
#else
    // HSTXの動作確認用に、SPI受信なしでも画面中央が見えるよう白い矩形を描いておく
    // (送信元に依存しない固定サイズの診断用矩形)。
    #define DIAG_W 320
    #define DIAG_H 240
    #define DIAG_LEFT ((SCREEN_W - DIAG_W) / 2)
    #define DIAG_TOP  ((SCREEN_H - DIAG_H) / 2)
    for (int row = 0; row < DIAG_H; row++) {
        memset(&framebuf[(DIAG_TOP + row) * SCREEN_W + DIAG_LEFT], 0xFF, DIAG_W);
    }
#endif

    // USB CDC(stdio_init_all())は実機で試したところ、HDMIモニタ接続直後から
    // NO SIGNALになるほどHSTXのリアルタイム性を壊すことが判明したため、
    // 使わない(hstx_irq_count/spi_irq_countは残しているが、読み出す手段は
    // 目視マーカーのみ)。

    hstx_init();
#if ENABLE_SPI_RX
    spi_slave_init();
    multicore_launch_core1(core1_copy_loop);

    static repeating_timer_t watchdog_timer;
    add_repeating_timer_ms(-WATCHDOG_INTERVAL_MS, hdmi_rx_watchdog_cb, NULL, &watchdog_timer);
#endif

    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    while (1) {
        __wfi();
    }
}
