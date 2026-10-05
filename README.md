# UART throughput test (nRF5340 DK)

Measures what the on-board debug probe's virtual COM port passes at 1 Mbaud, in both
directions. Firmware for the DK's application core on nrfx's HAL (`main.c`); a bash script on
the host (`throughput.sh`).

## nrfx path

```sh
export NRFX_DIR=/path/to/nrfx
```

Or pass it to CMake: `-DNRFX_DIR=/path/to/nrfx`. The value is cached in `build/`: after changing
it, pass `-DNRFX_DIR=` or delete `build/`.

CMSIS-Core headers are taken from `$NRFX_DIR/../../cmsis_6/CMSIS/Core/Include` (a Zephyr
workspace layout). Elsewhere: `-DCMSIS_DIR=/path/to/CMSIS/Core/Include`.

## Build

Needs `arm-none-eabi-gcc`, CMake 3.20+ and Ninja.

```sh
cmake -B build -G Ninja -DCMAKE_C_COMPILER=/path/to/arm-none-eabi-gcc
cmake --build build
```

`-DCMAKE_C_COMPILER` can be left out when `arm-none-eabi-gcc` is on `PATH`. Output:

| File | Flow control |
|---|---|
| `build/uart_throughput.{elf,hex}` | none |
| `build/uart_throughput_flow.{elf,hex}` | RTS/CTS |

Flow control is the firmware's choice: a CDC-ACM port has no host-side switch for it.

## Flash

With J-Link Commander
(`JLinkExe -device nRF5340_xxAA_APP -if SWD -speed 4000`):

```
loadfile build/uart_throughput.hex
r
g
```

## Measure

```sh
./throughput.sh /dev/cu.usbmodem<serial>3      # macOS; Linux: /dev/ttyACM1 (untested)
./throughput.sh <port> 4194304                 # optional: bytes to send, default 1048576
```

The port is the VCOM wired to the target's UART (P0.20 / P0.22). The script sends random bytes
to the target and prints the target's own count and timing; then the target streams 1 MiB of a
byte counter back and the script counts the gaps:

```
host -> MCU: 1048576 random bytes
  MCU:  rx 1048576 B in 10600 ms = 98917 B/s, errorsrc 0
MCU -> host: byte counter
  MCU:  tx 1048576 B in 10486 ms = 99996 B/s, errorsrc 0
  host: 1048576 of 1048576 B (100%) = 99997 B/s, 0 gaps
```

The line rate at 1 Mbaud is 100000 B/s. `errorsrc` is the target UARTE's error mask (1 overrun,
2 parity, 4 framing, 8 break). To compare with and without flow control, flash the other build
and run the script again.
