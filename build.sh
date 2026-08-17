#!/bin/bash
# HDMI受信ブリッジ ビルドスクリプト。
# PB-1000_emu_AG2/MSX_emu_pico2のどちらのMicroPythonビルドとも完全に独立。
# ローカルの pico-sdk (MicroPython rp2ポートが使っているのと同じチェックアウト) を再利用する。
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PICO_SDK_PATH="$HOME/projects/micropython/lib/pico-sdk"

BUILD_DIR="$SCRIPT_DIR/build"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake -DPICO_BOARD=pico2 ..
make -j"$(nproc)"

mkdir -p "$SCRIPT_DIR/firmware"
cp "$BUILD_DIR/hdmi_bridge_receiver.uf2" "$SCRIPT_DIR/firmware/hdmi_bridge_receiver.uf2"
echo "Built: $SCRIPT_DIR/firmware/hdmi_bridge_receiver.uf2"
