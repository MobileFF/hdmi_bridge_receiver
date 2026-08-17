# Build Guide

## Prerequisites

- A full pico-sdk checkout (a version that includes RP2350/`pico2` board
  support). `build.sh` defaults to `$HOME/projects/micropython/lib/pico-sdk`
  — designed to reuse the same checkout used by the MicroPython rp2 port
  build. If yours is elsewhere, either edit `PICO_SDK_PATH` inside
  `build.sh`, or override it as an environment variable before running:

  ```bash
  PICO_SDK_PATH=/path/to/pico-sdk ./build.sh
  ```

  (Note that `build.sh` currently hardcodes `export PICO_SDK_PATH=...`
  internally, so editing the script directly is the reliable way to use a
  different path.)

- CMake 3.13+, and the usual pico-sdk build toolchain (arm-none-eabi-gcc, etc).

This build is completely independent of the sender projects'
(PB-1000_emu_AG2/MSX_emu_pico2) MicroPython builds.

## Normal Build

```bash
./build.sh
```

What it does internally:

1. Recreates the `build/` directory (removing it first if it exists).
2. `cmake -DPICO_BOARD=pico2 ..`
3. `make -j$(nproc)`
4. Copies the resulting `hdmi_bridge_receiver.uf2` into the `firmware/`
   directory.

If you haven't changed the source, you can skip building entirely and flash
[`firmware/hdmi_bridge_receiver.uf2`](../firmware/) (the prebuilt file
included in the repository) as-is.

## Diagnostic Build (HSTX output only, SPI receive disabled)

If "no HDMI signal at all" after wiring, this build helps isolate whether
the problem is on the HSTX-init side or the SPI0-slave-receive side (GP0/1/2
wiring). It disables SPI receive and just shows a fixed white rectangle
centered on screen.

```bash
mkdir -p build_diag && cd build_diag
cmake -DPICO_BOARD=pico2 -DHDMI_ENABLE_SPI_RX=0 ..
make -j$(nproc)
```

This produces `build_diag/hdmi_bridge_receiver.uf2`. If flashing it shows
the white rectangle in the correct position, the HSTX/HDMI wiring side is
healthy — the problem can be narrowed down to the SPI0-receive side (wiring,
SPI mode, etc.).

## Flashing

1. Connect the Pico 2 over USB **while holding the BOOTSEL button**.
2. A drive named `RPI-RP2` appears.
3. Copy `firmware/hdmi_bridge_receiver.uf2` (or the relevant file for a
   diagnostic build) onto that drive (drag and drop).
4. It reboots automatically and HDMI output begins.

## Troubleshooting

| Symptom | What to check |
|---|---|
| No HDMI signal at all (monitor says NO SIGNAL) | Verify HSTX alone with the diagnostic build. If that also fails, check the HDMI addon's wiring/implementation |
| The diagnostic build shows the white rectangle but nothing shows in the normal build | Check the SPI wiring (GP0/1/2), the sender's SPI mode (mode 3 required), and the CS wiring |
| The top-left marker never changes | No packets are arriving from the sender. Check wiring and sender-side code |
| The marker changes but the image is corrupted/mispositioned | Verify the header is being built per the protocol spec ([protocol_en.md](protocol_en.md)), especially the width/height/scale values and the payload_len calculation |
| Fine right after boot but goes to NO SIGNAL after minutes to tens of minutes | Check whether the sender's SPI-send code does heavy work inside something equivalent to a Core0 ISR (this project itself has been validated on real hardware, but sender-side implementations can have this problem) |
