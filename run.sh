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
  echo 'Error: jq is not installed, install from https://stedolan.github.io/jq/download/'
  exit 1
fi

get_board() {
    board=$(pio device list --json-output | jq -c '.[] | select(.hwid != "n/a")' | fzy)
    hid=$(echo "$board" | jq -r ".hwid" | cut -d" " -f2 | cut -d"=" -f2)
    env_value=${deviceMap[$hid]:-"unknown"}
    port_value=$(echo "$board" | jq -r ".port")
    echo "$env_value|$port_value"
}

# @cmd build the project
build_all() {
    pio -f -c vim run
}

# @cmd build specific env
build() {
    IFS="|" read board_env board_port < <(get_board)
    pio -f -c vim run -e $board_env
}

# @cmd upload specific env
upload() {
    IFS="|" read board_env board_port < <(get_board)
    pio -f -c vim run -e $board_env -t upload --upload-port $board_port
}

# @cmd update project
update() {
    pio project init --ide vim && pio run --target compiledb
}

# @cmd monitor serial
# @alias monitor
serial() {
    IFS="|" read board_env board_port < <(get_board)
    pio device monitor --baud 115200 --port $board_port
}

eval "$(argc --argc-eval "$0" "$@")"
