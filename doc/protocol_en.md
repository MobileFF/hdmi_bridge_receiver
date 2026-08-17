# HDMI Bridge Protocol Specification

The complete wire-protocol specification exchanged between the sender (main
unit Pico/Pico W, etc.) and the receiver (the Pico 2 running this firmware)
over SPI1 (master) → SPI0 (slave). The implementation lives in `main.c`; a
sender-side implementation example is
[../../PB-1000_emu_AG2/src/lcd_controller.c](../../PB-1000_emu_AG2/src/lcd_controller.c)
(the `lcd_send_hdmi_*()` family of functions).

## Design Philosophy

Rather than baking each sender's fixed resolution, upscale factor, and bit
depth into the firmware, **every packet header self-describes itself**,
making the receiver firmware fully generic. This gives us:

- No receiver reflash needed when the sending project changes (e.g. MSX
  emulator ⇔ PB-1000 emulator).
- Hot-swap support: unplugging/replugging the sender Pico2 without power-
  cycling the receiver still works (see [architecture_en.md](architecture_en.md)).
- Automatic tracking when the sender's resolution changes mid-session (e.g.
  PB-1000's 32/64-dot toggle).

## Physical Layer

One CS-low interval = one packet = **an 8-byte header + a payload**.

- SPI mode 3 (CPOL=1, CPHA=1) is required. RP2350's PL022 SPI slave
  implementation has a known issue where mode 0 (CPOL=0/CPHA=0) loses sync
  after a few bytes (see [hardware_guide_en.md](hardware_guide_en.md)).
- The baud rate is arbitrary (validated on real hardware at 10MHz). Whatever
  value the sender sets via `spi_set_baudrate()` is used as-is — the receiver
  only sets an upper bound via `spi_init(spi0, 30MHz)`; the actual clock is
  always driven by the master (sender).
- Byte order is MSB first. All multi-byte numeric fields (width/height, etc.)
  are big-endian.

## Common Header Layout (8 bytes)

```
offset  0        1        2    3    4    5    6        7
field   pkt_type (type-dependent: see below)
```

The meaning of `[1]`–`[7]` depends on the packet type in `[0]`.

## Packet Type Overview

| Value | Name | Purpose |
|---|---|---|
| `0x00` | `PKT_PALETTE` | Palette update |
| `0x01` | `PKT_FRAME` | Pixel frame (game screen, etc.) |
| `0x02` | `PKT_TEXT_CMDS` | Text command stream (large UI, e.g. a menu) |
| `0x03` | `PKT_BEZEL_CMDS` | Text command stream (decorative overlay that coexists with the game screen, e.g. a bezel) |
| `0x04` | `PKT_CLEAR_SCREEN` | Clear the whole screen + reset centering tracking state |
| anything else | (unknown) | Interpreted as `PKT_FRAME` for forward compatibility |

Details of the header `[1]`–`[7]` and payload for each type follow.

---

### `PKT_PALETTE` (0x00)

```
[0] = 0x00
[1] = entry count (0 means 256; values over 256 are clamped to 256)
[2]-[7] = unused (0 recommended)
payload = entry_count bytes, 1 byte each = RGB332
```

The most recently received palette continues to be referenced by subsequent
`PKT_FRAME` packets (when bpp<8), `PKT_TEXT_CMDS`, and `PKT_BEZEL_CMDS`. The
palette does not need to be sent every frame — once is enough unless its
contents change.

**Example**: a 16-color palette with only index 0 = black and index 1 = white set

```
header:  00 10 00 00 00 00 00 00        (type=PALETTE, entry count=16)
payload: 00 FF 00 00 00 00 00 00 00 00 00 00 00 00 00 00
         (idx0=0x00 black, idx1=0xFF white, idx2-15=0x00)
```

---

### `PKT_FRAME` (0x01)

```
[0] = 0x01
[1] = bpp (1/2/4/8; any other value is treated as 8)
[2],[3] = width  (uint16, big-endian). 0 or >MAX_IMG_W(256) is clamped to 256
[4],[5] = height (uint16, big-endian). 0 or >MAX_IMG_H(192) is clamped to 192
[6] = scale (integer upscale factor. 0 means 1. >MAX_SCALE(4) is clamped to 4,
      and further reduced automatically so that width*scale/height*scale
      fits on screen (640x480))
[7] = reserved (0)
payload = width * height * bpp / 8 bytes (row-major)
```

- **bpp=8**: 1 byte/pixel is direct RGB332 (no palette lookup).
- **bpp=1/2/4**: each byte packs `8/bpp` pixels' worth of palette indices,
  MSB-first. E.g. at bpp=4, the high nibble of a byte is the pixel that
  comes first in the row, the low nibble is the next pixel's palette index
  (0-15). These are converted to RGB332 via the most recently received
  `PKT_PALETTE`.
- Bytes per row = `width * bpp / 8`. It's the sender's responsibility to
  keep `width` a multiple of `8/bpp` so this divides evenly (e.g. at bpp=4,
  width must be a multiple of 2).

**Example**: a 4x2 bpp=8 frame (2x upscale)

```
header: 01 08 00 04 00 02 02 00
        (type=FRAME, bpp=8, width=4, height=2, scale=2)
payload (8 bytes, direct RGB332, row-major):
  row0: E0 1C 03 FF   (red green blue white)
  row1: 00 00 00 00   (four blacks)
```

**Example**: a 4x2 bpp=4 frame (palette-indexed, 1x upscale)

```
header: 01 04 00 04 00 02 01 00
        (type=FRAME, bpp=4, width=4, height=2, scale=1)
payload (4 bytes, 2 pixels/byte, MSB-first):
  row0: 0x12 0x34   -> pixel(0,0)=idx1, (1,0)=idx2, (2,0)=idx3, (3,0)=idx4
  row1: 0x00 0x00   -> all idx0
```

---

### `PKT_TEXT_CMDS` (0x02) / `PKT_BEZEL_CMDS` (0x03)

Header and payload format are completely identical. **Only the type byte
differs** — the receiver tracks window-centering max-size (see "Display
Window Centering" below) independently for these two types.

```
[0] = 0x02 (PKT_TEXT_CMDS) or 0x03 (PKT_BEZEL_CMDS)
[1] = payload_len high byte
[2],[3] = width  (uint16, big-endian) — logical canvas width (for centering)
[4],[5] = height (uint16, big-endian) — logical canvas height
[6] = scale
[7] = payload_len low byte
payload = payload_len bytes of command stream
```

- `width`/`height` are not actual pixel data — they describe the "logical
  canvas" that the following commands' coordinates are relative to, used
  only for window-centering math. 0 becomes 1; values over 640/480 are
  clamped to 640/480 respectively.
- `payload_len` over `MAX_PAYLOAD` (49,152 bytes) is clamped. It's the
  sender's responsibility to keep the actual command stream under this
  limit — a full screen's worth is typically no more than a few hundred
  bytes.

The payload is a byte stream of any number of the following two command
types, laid out back to back. **Interpreted in order from the start,
processed through to the end**. If an unknown command ID is encountered,
processing stops there for safety (the command-stream boundary may have
been lost, so subsequent bytes are not interpreted).

#### `CMD_RECT` (0x00) — 10 bytes (including the leading command-ID byte)

```
[0] = 0x00
[1],[2] = x (uint16, big-endian, logical canvas coordinate)
[3],[4] = y (uint16, big-endian)
[5],[6] = w (uint16, big-endian)
[7],[8] = h (uint16, big-endian)
[9] = color (RGB332)
```

Fills a `w × h` rectangle at canvas coordinates `(x, y)` with `color`.

#### `CMD_TEXT` (0x01) — 8 + str_len bytes (including the leading command-ID byte)

```
[0] = 0x01
[1],[2] = x (uint16, big-endian, string start position)
[3],[4] = y (uint16, big-endian)
[5] = fg (foreground color, RGB332)
[6] = bg (background color, RGB332)
[7] = str_len (character count, 0-255)
[8..] = character code sequence (str_len bytes, ASCII)
```

Each character code is expanded to 8x8 pixels via `font_petme128_8x8[]`
(the same font used by MicroPython's built-in `framebuf` module, MIT
license), drawn left to right advancing `8 * scale` pixels per character.
Character codes range 32 (space) to 127 (DEL). Out-of-range values fall back
to code 127 (a checkerboard pattern), matching MicroPython's own
`framebuf.text()` behavior.

**Example**: a command drawing white text "Hi" on a black background at `(x=4, y=4)`

```
01 00 04 00 04 FF 00 02 48 69
(CMD_TEXT, x=4, y=4, fg=white(0xFF), bg=black(0x00), str_len=2, "Hi"=0x48,0x69)
```

A complete payload example with a background-clearing rect placed before it
(320x240 canvas, 2x upscale):

```
header:  02 00 01 40 00 F0 02 14
         (type=TEXT_CMDS, payload_len=20, width=320, height=240, scale=2)
payload (20 bytes):
  00 00 00 00 00 01 40 00 F0 00     (CMD_RECT: fills (0,0)-(320,240) with black. 10 bytes)
  01 00 04 00 04 FF 00 02 48 69     (CMD_TEXT: the "Hi" above. 10 bytes)
```

(Header `[1]` = payload_len high byte = 0x00, `[7]` = low byte = 0x14 →
0x0014 = 20. CMD_RECT is 10 bytes, CMD_TEXT is 8+2=10 bytes, total 20 bytes.)

---

### `PKT_CLEAR_SCREEN` (0x04)

```
[0] = 0x04
[1]-[7] = unused (0 recommended)
payload = 1 dummy byte (content is ignored)
```

Clears the whole screen (640x480) to black and fully resets the centering-
tracking state (see below) for all kinds (`KIND_PIXEL`/`KIND_TEXT`/
`KIND_BEZEL`). Used to clear away the previous session's leftover display
when, for example, the sender's main Pico2 has just been reset (the
receiver, which stays powered independently, would otherwise keep showing
stale content). Typical usage is for the sender to send this right at the
start of boot, immediately after HDMI initialization completes.

**Why a dedicated packet**: sending a large black rectangle via
`PKT_TEXT_CMDS` etc. instead would inflate that `KIND`'s own tracked max
size to 640x480, causing subsequent, genuinely small frames of that same
type to be centered with the wrong window size (a bug that actually
happened). `PKT_CLEAR_SCREEN` is designed as a fully independent operation
that has zero effect on any `KIND`'s tracking.

---

## Display Window Centering

The receiver treats `PKT_FRAME`/`PKT_TEXT_CMDS`/`PKT_BEZEL_CMDS` as three
independent "kinds" (`KIND_PIXEL`/`KIND_TEXT`/`KIND_BEZEL`), and **for each
kind separately remembers "the largest width/height received so far," using
that as the basis for centering its window on screen (640x480)**.

```
win_w = (this kind's max width)  * scale   (clamped to screen width if it would exceed it)
win_h = (this kind's max height) * scale   (clamped to screen height if it would exceed it)
left_margin = (640 - win_w) / 2
top_margin  = (480 - win_h) / 2
```

- The max value **only ever increases**. Even if the sender's resolution
  shrinks (e.g. PB-1000's 64→32 dot toggle), the window itself stays at its
  largest-ever size, so its on-screen position never moves — the vacated
  area is simply cleared to black.
- **Being independent per kind is the crucial point**. If all kinds shared a
  single tracking variable, once a larger piece of content (e.g. a
  `PKT_TEXT_CMDS` menu screen, 480x320) had ever been shown, a subsequent
  smaller one (e.g. a `PKT_FRAME` game screen, 192x64) would have its window
  dragged toward the larger one's size and end up shifted to the top-left of
  the screen — a bug that actually happened. The three kinds
  (`KIND_PIXEL`/`KIND_TEXT`/`KIND_BEZEL`) are treated as separate layers
  that can overlap and coexist; each layer clears only its own
  previously-drawn area before drawing the next.
- `PKT_BEZEL_CMDS` is designed to be sent in **the exact same logical
  coordinate system and scale** as `PKT_FRAME` (the game screen), so that
  even though it's centered independently, it ends up concentric with — and
  visually wraps symmetrically around — the game screen on screen (see the
  sender-side implementation example `_draw_bezel_hdmi()`).

## Implementation Constants

| Constant | Value | Meaning |
|---|---|---|
| `SCREEN_W` / `SCREEN_H` | 640 / 480 | HDMI output resolution (fixed, 640x480@60Hz) |
| `MAX_IMG_W` / `MAX_IMG_H` | 256 / 192 | `PKT_FRAME`'s width/height cap (values over this are clamped) |
| `MAX_SCALE` | 4 | Cap on the scale field (values over this are clamped) |
| `MAX_PAYLOAD` | 49,152 bytes | Size of one receive-buffer face. Derived from `PKT_FRAME`'s largest possible payload at bpp=8 (256×192). `PKT_TEXT_CMDS`/`PKT_BEZEL_CMDS`'s payload_len is also clamped to this range (the sender must not exceed it) |
| `MAX_PALETTE_ENTRIES` | 256 | Cap on the number of palette entries |

`PKT_TEXT_CMDS`/`PKT_BEZEL_CMDS`'s width/height are unrelated to
`MAX_IMG_W`/`MAX_IMG_H` (the screen size, 640x480, is the actual cap) — you
can send the real panel resolution as-is (e.g. 480x320). Since the payload
itself is a command stream rather than being proportional to resolution, it
only needs to fit within `MAX_PAYLOAD` (the receive-buffer size).

## Undefined Behavior / Forward Compatibility

- Unknown packet types (`0x05` and above) are interpreted as `PKT_FRAME`.
- Unknown command IDs inside a `PKT_TEXT_CMDS`/`PKT_BEZEL_CMDS` payload stop
  processing at that point (subsequent bytes are not interpreted, since the
  command-stream boundary may have been lost).
- If transmission is interrupted partway through a header or payload (e.g.
  the sender is physically unplugged), the receiver's watchdog detects and
  recovers automatically. See [architecture_en.md](architecture_en.md) for
  details.
