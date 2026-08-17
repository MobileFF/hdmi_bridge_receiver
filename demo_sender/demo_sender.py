"""
hdmi_bridge_receiver 汎用送信サンプル (MicroPython)

このスクリプトは、特定のエミュレータプロジェクトに一切依存しない、素の
MicroPython(machine.SPI / machine.Pin のみ)で書かれた送信側の実装例。
hdmi_bridge_receiver を書き込んだ第2Pico 2に接続して実行すると、5種類の
パケット(PKT_CLEAR_SCREEN / PKT_PALETTE / PKT_FRAME / PKT_TEXT_CMDS /
PKT_BEZEL_CMDS)を順にデモ表示する。プロトコルの詳細は
../doc/protocol.md を参照 — このファイルはそこに書かれている仕様の
実装例そのものなので、読みながら見比べると理解しやすい。

## 使い方

1. ../doc/hardware_guide.md の配線に従って接続する。
2. 下記の「配線設定」を実際のピン配置に合わせて書き換える
   (デフォルトはPB-1000_emu_AG2/MSX_emu_pico2と同じSPI1+GP28の組み合わせ)。
3. 送信側Pico(何でもよい、MicroPythonが書き込まれていれば十分)へ転送し、
   `mpremote run demo_sender.py` で実行するか、REPLで
   `import demo_sender; demo_sender.main()` する。

Ctrl-Cで停止できる。
"""
from machine import SPI, Pin
import time

# ==== 配線設定 (実際の配線に合わせて書き換えてください) ====
SPI_ID = 1
PIN_SCK = 10
PIN_MOSI = 11
PIN_CS = 28
BAUDRATE = 10_000_000  # 実機検証済みの値。SPIモード3(CPOL=1/CPHA=1)固定

# ==== プロトコル定数 (doc/protocol.md 参照) ====
PKT_PALETTE = 0x00
PKT_FRAME = 0x01
PKT_TEXT_CMDS = 0x02
PKT_BEZEL_CMDS = 0x03
PKT_CLEAR_SCREEN = 0x04

CMD_RECT = 0x00
CMD_TEXT = 0x01


class HDMIBridge:
    """8byteヘッダー+ペイロードのパケット送信をカプセル化した薄いラッパー。"""

    def __init__(self, spi_id=SPI_ID, sck=PIN_SCK, mosi=PIN_MOSI, cs=PIN_CS,
                 baudrate=BAUDRATE):
        self.spi = SPI(spi_id, baudrate=baudrate, polarity=1, phase=1,
                        sck=Pin(sck), mosi=Pin(mosi))
        self.cs = Pin(cs, Pin.OUT, value=1)  # アイドルhigh

    def _send(self, header, payload=b""):
        self.cs.value(0)
        self.spi.write(header)
        if payload:
            self.spi.write(payload)
        self.cs.value(1)

    def clear_screen(self):
        """PKT_CLEAR_SCREEN — 画面全体クリア+中央寄せ追跡リセット。"""
        header = bytes((PKT_CLEAR_SCREEN, 0, 0, 0, 0, 0, 0, 0))
        self._send(header, b"\x00")  # ダミー1byteペイロード

    def send_palette(self, entries):
        """PKT_PALETTE — entries: RGB332値のリスト/bytes(最大256個)。"""
        n = len(entries) & 0xFF  # 0は256扱い(呼び出し側で0個は渡さない前提)
        header = bytes((PKT_PALETTE, n, 0, 0, 0, 0, 0, 0))
        self._send(header, bytes(entries))

    def send_frame(self, width, height, scale, bpp, pixel_data):
        """PKT_FRAME — bpp=8なら1byte/pixel直接RGB332、bpp=1/2/4ならパレット
        参照(直近のsend_palette()の内容を使う)。pixel_dataは行優先。"""
        header = bytes((
            PKT_FRAME, bpp,
            (width >> 8) & 0xFF, width & 0xFF,
            (height >> 8) & 0xFF, height & 0xFF,
            scale, 0,
        ))
        self._send(header, pixel_data)

    def _send_cmds(self, pkt_type, width, height, scale, payload):
        n = len(payload)
        header = bytes((
            pkt_type, (n >> 8) & 0xFF,
            (width >> 8) & 0xFF, width & 0xFF,
            (height >> 8) & 0xFF, height & 0xFF,
            scale, n & 0xFF,
        ))
        self._send(header, payload)

    def send_text_cmds(self, width, height, scale, payload):
        """PKT_TEXT_CMDS — payloadはcmd_rect()/cmd_text()で組み立てたバイト列
        を連結したもの。width/heightは論理キャンバスサイズ(中央寄せ用)。"""
        self._send_cmds(PKT_TEXT_CMDS, width, height, scale, payload)

    def send_bezel_cmds(self, width, height, scale, payload):
        """PKT_BEZEL_CMDS — send_text_cmds()と書式は同じだが、受信側が
        ウィンドウ中央寄せの追跡をPKT_TEXT_CMDSと独立させる別種別。
        send_frame()と同じ論理座標系・scaleで送ると、ゲーム画面を
        対称に一回り包む位置に重ねて表示できる(demo_bezel()参照)。"""
        self._send_cmds(PKT_BEZEL_CMDS, width, height, scale, payload)


# ==== コマンド列ビルダー (doc/protocol.md の CMD_RECT / CMD_TEXT) ====

def cmd_rect(x, y, w, h, color):
    """矩形塗りコマンド(10byte)。colorはRGB332。"""
    return bytes((
        CMD_RECT,
        (x >> 8) & 0xFF, x & 0xFF,
        (y >> 8) & 0xFF, y & 0xFF,
        (w >> 8) & 0xFF, w & 0xFF,
        (h >> 8) & 0xFF, h & 0xFF,
        color,
    ))


def cmd_text(x, y, text, fg, bg):
    """文字列描画コマンド(8+len(text)byte)。fg/bgはRGB332。"""
    data = text.encode()
    if len(data) > 255:
        data = data[:255]
    return bytes((
        CMD_TEXT,
        (x >> 8) & 0xFF, x & 0xFF,
        (y >> 8) & 0xFF, y & 0xFF,
        fg, bg, len(data),
    )) + data


# ==== デモシーン ====
# ゲーム画面デモ(色帯/パレットストライプ)とベゼルデモは、あえて同じ論理
# 座標系(FRAME_W x FRAME_H)・同じscaleを使っている — こうすると受信側で
# 独立に中央寄せされる2つのウィンドウが、画面上ではちょうど同じ中心を
# 共有し、ベゼルがゲーム画面を対称に縁取る形で重なる(doc/protocol.mdの
# 「表示ウィンドウの中央寄せ」参照)。

FRAME_W, FRAME_H, FRAME_SCALE = 160, 120, 3
MENU_W, MENU_H, MENU_SCALE = 320, 240, 2

_BAR_COLORS = (0xE0, 0xFC, 0x1C, 0x03, 0x1F, 0xE3, 0xFF, 0x00)  # 赤 橙 緑 青 水 紫 白 黒


def demo_color_bars(bridge, offset=0):
    """PKT_FRAME (bpp=8直接色) — 8色の縦帯。offsetを変えると横スクロールする。"""
    bar_w = FRAME_W // len(_BAR_COLORS)
    row = bytearray(FRAME_W)
    for x in range(FRAME_W):
        bar = ((x + offset) // bar_w) % len(_BAR_COLORS)
        row[x] = _BAR_COLORS[bar]
    pixels = bytes(row) * FRAME_H
    bridge.send_frame(FRAME_W, FRAME_H, FRAME_SCALE, 8, pixels)


# 16エントリパレット(先頭の黒は使わず、1〜7を彩色、残りは黒で埋める)
_DEMO_PALETTE = bytes((0x00, 0xE0, 0x1C, 0x03, 0xFF, 0xFC, 0x1F, 0xE3) + (0x00,) * 8)


def demo_palette_stripes(bridge, phase=0):
    """PKT_FRAME (bpp=4パレット参照) — 斜めストライプ。毎回パレットも
    再送しているが、内容が変わらないなら初回だけで十分(デモなので簡略化)。"""
    bridge.send_palette(_DEMO_PALETTE)
    w, h = FRAME_W, FRAME_H
    row_bytes = w // 2  # 1byteに2pixel
    buf = bytearray(row_bytes * h)
    for y in range(h):
        for xb in range(row_bytes):
            i0 = ((xb * 2 + y + phase) // 8) % 7 + 1
            i1 = ((xb * 2 + 1 + y + phase) // 8) % 7 + 1
            buf[y * row_bytes + xb] = (i0 << 4) | i1
    bridge.send_frame(w, h, FRAME_SCALE, 4, bytes(buf))


def demo_bezel(bridge):
    """PKT_BEZEL_CMDS — send_frame()と同じ論理座標系・scaleで枠を描く。
    外枠・中間の2層のみ塗り、中央(FRAME_W x FRAME_H)は塗らずに残す —
    その後にゲーム画面(demo_color_bars等)を送ると、ちょうどその領域を
    上書きする形で重なるため、必ずこの関数を先に呼ぶこと(呼び出し順は
    main()参照)。"""
    pad = 6
    payload = b"".join((
        cmd_rect(0, 0, FRAME_W + pad * 2, FRAME_H + pad * 2, 0x92),         # 外枠(暗い青紫)
        cmd_rect(pad // 2, pad // 2, FRAME_W + pad, FRAME_H + pad, 0xB6),   # 中間(明るいグレー)
    ))
    bridge.send_bezel_cmds(FRAME_W + pad * 2, FRAME_H + pad * 2, FRAME_SCALE, payload)


def demo_menu(bridge, counter=0):
    """PKT_TEXT_CMDS — メニュー画面風のデモ(矩形塗り+文字列描画)。"""
    BG, FG, HDR, SEL_BG, SEL_FG = 0x00, 0xFF, 0xFE, 0x92, 0xFF
    payload = b"".join((
        cmd_rect(0, 0, MENU_W, MENU_H, BG),
        cmd_text(4, 4, "hdmi_bridge_receiver DEMO", HDR, BG),
        cmd_rect(0, 24, MENU_W, 16, SEL_BG),
        cmd_text(4, 26, "> PKT_TEXT_CMDS", SEL_FG, SEL_BG),
        cmd_text(4, 44, "  rect fill + text commands", FG, BG),
        cmd_text(4, 64, "counter: %d" % counter, FG, BG),
        cmd_text(4, MENU_H - 12, "see doc/protocol.md", 0x92, BG),
    ))
    bridge.send_text_cmds(MENU_W, MENU_H, MENU_SCALE, payload)


def main():
    bridge = HDMIBridge()
    print("hdmi_bridge_receiver demo sender: starting (Ctrl-C to stop)")
    bridge.clear_screen()
    time.sleep_ms(300)  # 起動直後の取りこぼし対策(main.pyの実装例と同じ配慮)

    scene = 0
    frame = 0
    try:
        while True:
            if scene == 0:
                demo_bezel(bridge)          # 先にベゼル(枠)を送る
                demo_color_bars(bridge, offset=frame)  # 次にゲーム画面(枠の内側に重なる)
            elif scene == 1:
                demo_bezel(bridge)
                demo_palette_stripes(bridge, phase=frame)
            else:
                demo_menu(bridge, counter=frame)

            frame += 1
            if frame % 40 == 0:
                scene = (scene + 1) % 3
                print("switching to scene", scene)

            time.sleep_ms(100)
    except KeyboardInterrupt:
        print("stopped")


if __name__ == "__main__":
    main()
