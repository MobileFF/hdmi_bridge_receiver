cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350)

include(${CMAKE_CURRENT_LIST_DIR}/pico_sdk_import.cmake)

project(hdmi_bridge_receiver C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(hdmi_bridge_receiver
    main.c
)

# 切り分け用診断ビルド: -DHDMI_ENABLE_SPI_RX=0 でSPI0スレーブ受信を無効化し、
# HSTXのDVI出力だけの最小構成にする(信号が全く出ない問題の原因切り分け用)。
set(HDMI_ENABLE_SPI_RX 1 CACHE STRING "1=SPI0受信を有効化(通常), 0=HSTXのみの診断ビルド")
target_compile_definitions(hdmi_bridge_receiver PRIVATE ENABLE_SPI_RX=${HDMI_ENABLE_SPI_RX})

target_link_libraries(hdmi_bridge_receiver
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_dma
    hardware_spi
    hardware_irq
    hardware_structs
)

pico_add_extra_outputs(hdmi_bridge_receiver)
