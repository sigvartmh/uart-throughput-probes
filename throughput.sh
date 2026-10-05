#!/bin/bash
# Host side of the UART throughput test (main.c): both directions through the probe's virtual COM
# port at 1 Mbaud.
#   ./throughput.sh <port> [bytes]
#       port:  the VCOM wired to the target's UART: /dev/cu.usbmodem<serial>3 (macOS),
#              /dev/ttyACM1 (Linux)
#       bytes: random bytes to send to the target, default 1048576
# Flow control is the firmware's side of it: flash uart_throughput.elf (none) or
# uart_throughput_flow.elf (RTS/CTS) and run this again. A CDC-ACM port has no switch for it, and
# host to probe is flow-controlled by USB either way.
set -u
port=${1:?usage: $0 <port> [bytes]}
bytes=${2:-1048576}
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

exec 3<>"$port" # held open throughout: macOS forgets the settings on the last close
stty raw -echo clocal <&3
# macOS's termios refuses non-standard speeds; its IOSSIOSPEED ioctl takes them.
stty 1000000 <&3 2>/dev/null ||
    perl -e '$speed = pack("L", 1000000); ioctl(STDIN, 0x80045402, $speed) or die "IOSSIOSPEED: $!\n"' <&3 || exit 1

# Whatever is still on its way in (a report nobody read, a stream started by a glitch on open).
while IFS= read -r -t 1 -n 4096 _ <&3; do :; done

echo "host -> MCU: $bytes random bytes"
head -c "$bytes" /dev/urandom >&3
line=
IFS= read -r -t 10 line <&3
echo "  MCU:  ${line:-no report}" | tr -d '\r'

echo "MCU -> host: byte counter"
printf t >&3 # a single byte starts the stream
cat <&3 >"$tmp" &
reader=$!
size=-1
# Until nothing has arrived for 2 s (the MCU's closing line comes 1 s after the stream).
while sleep 2 && new=$(($(wc -c <"$tmp"))) && [ "$new" != "$size" ]; do size=$new; done
kill $reader 2>/dev/null
wait $reader 2>/dev/null

closing=$(tail -c 96 "$tmp" | LC_ALL=C grep -a -o 'tx [0-9]* B in [0-9]* ms = [0-9]* B/s, errorsrc [0-9]*')
if [ -z "$closing" ]; then
    echo "  got $size B and no closing line from the MCU"
    exit 1
fi
# shellcheck disable=SC2086  # split into words: tx <sent> B in <ms> ms ...
set -- $closing
sent=$2 ms=$5
got=$((size - ${#closing} - 4)) # the line has \r\n on both sides
gaps=$(head -c "$got" "$tmp" | od -An -v -tu1 | awk '
    { for (i = 1; i <= NF; i++) { if (n++ && $i != (prev + 1) % 256) gaps++; prev = $i } }
    END { print gaps + 0 }')
echo "  MCU:  $closing"
echo "  host: $got of $sent B ($((got * 100 / sent))%) = $((got * 1000 / ms)) B/s, $gaps gaps"
