#!/bin/bash

# Declare an associative array named deviceMap
declare -A deviceMap
deviceMap["1A86:55D4"]="ttgo"
deviceMap["239A:800B"]="m0"

#check if argc executable is available
if ! [ -x "$(command -v argc)" ]; then
  echo 'Error: argc is not installed, install from https://github.com/sigoden/argc'
  exit 1
elif ! [ -x "$(command -v pio)" ]; then
  echo 'Error: pio is not installed, install from https://platformio.org/install/cli'
  exit 1
elif ! [ -x "$(command -v jq)" ]; then
  echo 'Error: jq is not installed, install from https://github.com/jqlang/jq'
  exit 1
elif ! [ -x "$(command -v gum)" ]; then
  echo 'Error: gum is not installed, install from https://github.com/charmbracelet/gum'
  exit 1
fi

get_board() {
    board=$(pio device list --json-output | jq -c '.[] | select(.hwid != "n/a")' | gum choose)
    if [ -z "$board" ]; then
        echo "No board found"
        exit 1
    fi
    hid=$(echo "$board" | jq -r ".hwid" | cut -d" " -f2 | cut -d"=" -f2)
    serial_value=$(echo "$board" | jq -r ".hwid" | cut -d" " -f3 | cut -d"=" -f2)
    env_value=${deviceMap[$hid]:-"unknown"}
    port_value=$(echo "$board" | jq -r ".port")
    echo "$env_value|$port_value|$serial_value"
}

# @cmd build the project
# @alias b
build() {
    if [ -z "$BOARD_ENV" ]; then
        echo "BOARD_ENV is not set, building all"
        pio -f -c vim run
    else
        pio -f -c vim run -e $BOARD_ENV
    fi
}

# @cmd upload to board
# @alias u
upload() {
    IFS="|" read board_env board_port board_serial < <(get_board)
    export BOARD_ENV=$board_env
    export BOARD_PORT=$board_port
    export BOARD_SERIAL=$board_serial
    gum confirm --default "Do you want to upload to device??" && pio -f -c vim run -e $board_env -t upload --upload-port $board_port
}

# @cmd refresh build files
# @alias r
refresh() {
    pio project init --ide vim && pio run --target compiledb
}

# @cmd monitor the serial port
# @alias m
monitor() {
    IFS="|" read board_env board_port board_serial < <(get_board)
    export BOARD_ENV=$board_env
    export BOARD_PORT=$board_port
    export BOARD_SERIAL=$board_serial
    pio device monitor --baud 115200 --port $board_port
}


# @cmd test with script/send.py
# @alias t
test() {
    gum input --placeholder="FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF"
    python3 script/send.py
}

eval "$(argc --argc-eval "$0" "$@")"
