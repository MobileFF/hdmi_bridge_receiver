# Internal Implementation Notes

`main.c`'s internal design, and constraints learned from real-hardware
validation. See [protocol_en.md](protocol_en.md) for the protocol
specification and [hardware_guide_en.md](hardware_guide_en.md) for wiring.

## Overall Structure: Core0 (Receive) / Core1 (Render) Split

RP2350's two cores are divided as follows:

- **Core0**: the SPI0 slave's DMA receive interrupt handler
  (`spi_rx_dma_irq_handler()`), and the HSTX scanout DMA interrupt handler
  (`hstx_dma_irq_handler()`).
- **Core1**: the actual rendering of received packets
  (`core1_copy_loop()`).

**This split is a mandatory design based on a constraint discovered through
real-hardware testing**: doing heavy work (e.g. copying pixels into the
framebuffer via memcpy) inside Core0's SPI-receive interrupt handler was
found on real hardware to interfere with HSTX's scanline reconfiguration
(which happens roughly every 32μs), causing loss of the HDMI signal (NO
SIGNAL) after anywhere from a few minutes to tens of minutes.

Because of this, Core0's ISR does nothing beyond "re-arm the DMA" and
"notify Core1 via the inter-core FIFO." All actual heavy work (pixel
unpacking, font rendering, etc.) is offloaded to Core1.

## SPI Receive: Double Buffering + Two-Stage Header/Payload Reception

Two buffer faces, `scratch_buf[2][MAX_PAYLOAD]`, are used ping-pong style:

1. At boot, DMA is armed to wait in the "receiving the 8-byte header" state.
2. When header reception completes (DMA completion interrupt), the packet
   type determines how many payload bytes to expect, and DMA is re-armed
   toward `scratch_buf[current face]`.
3. When payload reception completes, metadata is committed into `buf_meta[]`
   (below), the face is swapped, and Core1 is notified via the inter-core
   FIFO.
4. Back to waiting for the next header (step 1).

Since Core1 only ever reads the face that DMA is *not* currently writing
into, reception and rendering proceed concurrently.

## Core0→Core1 Communication: the `frame_meta_t` Struct

The initial implementation bit-packed width/height/scale etc. directly into
the 32-bit notification message sent to Core1, but once
`PKT_TEXT_CMDS`/`PKT_BEZEL_CMDS`'s (variable-length) `payload_len` needed to
be conveyed too, there was no room left, so this was switched to a
`buf_meta[2]` array of `volatile frame_meta_t`:

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

When payload reception completes, Core0 assigns
`buf_meta[face index] = pending_meta`, then sends just the "face index" over
the inter-core FIFO (the SIO FIFO push/pop operations carry an inter-core
ordering guarantee, so this is sufficient). Core1 receives the face index
and then reads `buf_meta[face index]`.

## Display Window Centering (`KIND_*`)

See the corresponding section in [protocol_en.md](protocol_en.md). The key
implementation point is that both the "largest size received so far"
tracking (`max_w[3]`/`max_h[3]`) and the "region actually drawn last time"
tracking (`last_left[3]`/`last_top[3]`/`last_ww[3]`/`last_wh[3]`) are kept
**fully independent across the three kinds**
(`KIND_PIXEL`/`KIND_TEXT`/`KIND_BEZEL`).

This design followed hitting two bugs on real hardware:

1. Tracking all kinds with a single max_w/max_h: after showing large content
   (a menu) even once, subsequent small content (the game screen) would
   shift toward the top-left.
2. Tracking all kinds with a single last_left/last_top etc.: a new game-
   screen frame would incorrectly clear the previously drawn bezel's area
   (because its size differed), making the bezel appear to vanish.

Both were solved by treating each kind as "a separate layer that can
overlap and coexist with the others," where each layer only ever clears its
own previously-drawn area before drawing the next.

## Hot-Swap Monitoring (`hdmi_rx_watchdog_cb()`)

Unplugging/replugging the sender Pico2 without power-cycling it can leave
the SPI clock stopping right in the middle of a header or payload. Since DMA
is simply waiting for "N more bytes and then it's done," if the rest
physically never arrives, the completion interrupt never fires, and
reception stays stuck forever.

To detect this, a roughly 50ms-interval timer is set up via
`add_repeating_timer_ms()`, which monitors the DMA's remaining transfer
count (`dma_hw->ch[SPI_RX_DMA_CHAN].transfer_count`):

- The state "waiting for a header, not a single byte received yet" (i.e.
  either nothing is connected, or it's simply, legitimately waiting for the
  next packet) is never considered abnormal no matter how long it persists.
- In any other state, if the remaining transfer count doesn't change for 3
  consecutive checks (~150ms), the DMA is forcibly aborted via
  `dma_channel_abort()`, any stray leftover bytes in SPI0's RX FIFO are
  drained, and the state is reset to waiting for a header.

The CS pin (GP1) also has a weak pull-up (`gpio_pull_up()`) applied, which
reduces misbehavior from the CS line momentarily floating during the
sender's GPIO initialization (just insurance — actual recovery is the
watchdog's job).

## Font Rendering (`draw_glyph()`)

`PKT_TEXT_CMDS`/`PKT_BEZEL_CMDS`'s CMD_TEXT command only sends character
codes; the actual glyph rendering happens on the receiver side. The font
data used (`font_petme128_8x8.h`) is MicroPython's own
`extmod/font_petme128_8x8.h` verbatim (MIT license, Copyright (c) 2013, 2014
Damien P. George).

The format is a column-major bitmap where "1 byte = 1 column (8 pixels),
LSB at the top" (the same interpretation as MicroPython's
`framebuf.FrameBuffer.text()`). Rather than rasterizing with
`framebuf.FrameBuffer.text()` on the sender side and then sending pixels,
sending just the character codes significantly reduces the sender's burden
(RAM and processing time).

## Key Real-Hardware Constraints (Recap)

- **Don't use `stdio_init_all()` (USB CDC)**: found on real hardware to
  break HSTX's real-time behavior badly enough that the monitor goes to NO
  SIGNAL immediately upon connection. Debug output relies solely on the
  top-left 16x16 visual marker (red/green toggle per packet received). The
  `hstx_irq_count`/`spi_irq_count` diagnostic counters remain in the code,
  but no means of reading them out is provided.
- **SPI mode 3 required**: see [hardware_guide_en.md](hardware_guide_en.md).
- **Bus priority**: DMA read/write priority is raised via
  `bus_ctrl_hw->priority`
  (`BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS`).

## About the HSTX/DVI Output Itself

Reuses the official pico-examples (`raspberrypi/pico-examples`'s
`hstx/dvi_out_hstx_encoder/dvi_out_hstx_encoder.c`, Copyright (c) 2024
Raspberry Pi (Trading) Ltd.) 640x480 RGB332 output code almost verbatim. See
that code and the official documentation for the details of TMDS encoding,
timing generation, and ping-pong-DMA-driven scanout. The only
project-specific change is swapping the read source for the
`framebuf[]` array that gets filled in by SPI reception.
