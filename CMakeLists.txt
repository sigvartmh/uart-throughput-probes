# UART throughput test firmware for the nRF5340 DK's application core, on nrfx's HAL.
#   cmake -B build -G Ninja -DCMAKE_C_COMPILER=/path/to/arm-none-eabi-gcc
#   cmake --build build
#     -> build/uart_throughput.{elf,hex}        no flow control
#        build/uart_throughput_flow.{elf,hex}   RTS/CTS
# Then throughput.sh on the host. See README.md.
cmake_minimum_required(VERSION 3.20)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
if(NOT CMAKE_C_COMPILER)
  set(CMAKE_C_COMPILER arm-none-eabi-gcc)
endif()

project(uart_throughput C)

# nrfx: -DNRFX_DIR=, else the NRFX_DIR environment variable, else this machine's Zephyr workspace.
if(DEFINED ENV{NRFX_DIR})
  set(nrfx_default "$ENV{NRFX_DIR}")
else()
  set(nrfx_default /Users/siho/dev/vanilla-zephyr/zephyrproject/modules/hal/nordic/nrfx)
endif()
set(NRFX_DIR "${nrfx_default}" CACHE PATH "nrfx 4.x")
set(CMSIS_DIR ${NRFX_DIR}/../../cmsis_6/CMSIS/Core/Include CACHE PATH "CMSIS-Core headers")

set(CPU -mcpu=cortex-m33 -mthumb)

function(firmware name flow)
  add_executable(${name}.elf
    main.c
    startup.c
    ${NRFX_DIR}/bsp/stable/mdk/nrf53/nrf5340/system_nrf5340_application.c)
  # NRF_SKIP_FICR_NS_COPY_TO_RAM: all of this is secure code. Without it SystemInit copies FICR
  # into the top 4 KiB of RAM for non-secure code, which is where the stack is.
  target_compile_definitions(${name}.elf PRIVATE
    NRF5340_XXAA_APPLICATION NRF_SKIP_FICR_NS_COPY_TO_RAM FLOW=${flow})
  target_include_directories(${name}.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}        # nrfx_config.h
    ${NRFX_DIR}
    ${NRFX_DIR}/templates              # nrfx_glue.h: the empty template does for the HAL
    ${NRFX_DIR}/bsp/stable
    ${NRFX_DIR}/bsp/stable/templates
    ${CMSIS_DIR})
  target_compile_options(${name}.elf PRIVATE ${CPU} -Os -g -Wall -Wextra -ffunction-sections -fdata-sections)
  target_link_options(${name}.elf PRIVATE ${CPU} -nostartfiles --specs=nano.specs
    -T${CMAKE_CURRENT_SOURCE_DIR}/nrf5340.ld -Wl,--gc-sections)
  add_custom_command(TARGET ${name}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${name}.elf> ${name}.hex)
endfunction()

firmware(uart_throughput 0)
firmware(uart_throughput_flow 1)
