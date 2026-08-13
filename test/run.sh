#!/bin/sh
# Host build and smoke test for the Melodi modem firmware logic.
set -eu
cd "$(dirname "$0")/.."
proto=../melodi/src/proto
board=${1:-ARDUINO_ADAFRUIT_FEATHER_RP2040_RFM}
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

cc -std=gnu11 -Wall -Wextra -Werror -O1 -I"$proto" \
    -c "$proto/radio.c" -o "$out/radio.o"
c++ -std=c++17 -Wall -Wextra -Werror -O1 -D "$board" \
    -Itest -Iinclude -I"$proto" \
    -o "$out/test_modem" test/test_modem.cpp src/modem.cpp "$out/radio.o"
"$out/test_modem"
