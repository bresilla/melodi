#!/bin/bash

# Declare an associative array named deviceMap
declare -A deviceMap
deviceMap["1A86:55D4"]="ttgo"
deviceMap["239A:800B"]="m0"
deviceMap["239A:80F1"]="rpi"

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
    board=$(pio device list --json-output | jq -c '.[] | select(.hwid != "n/a")' | gum choose --limit=1 --select-if-one)
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
# @option    --ipv6 <IPV6>    IPv6 address to send to
# @flag      --no-confirm     Don't ask for confirmation before uploading
# @flag      --monitor        Monitor after upload
upload() {
    IFS="|" read board_env board_port board_serial < <(get_board)
    export BOARD_ENV=$board_env
    export BOARD_PORT=$board_port
    export BOARD_SERIAL=$board_serial
    if ! [ -z "$argc_ipv6" ]; then
        export BOARD_IPV6=$argc_ipv6
    fi

    if ! [ -z "$argc_no_confirm" ]; then
        pio -f -c vim run -e $board_env -t upload --upload-port $board_port
    else
        gum confirm --default "Do you want to upload to device \"$board_env\" at port \"$board_port\"??" && pio -f -c vim run -e $board_env -t upload --upload-port $board_port
        if ! [ -z "$argc_monitor" ]; then
            sleep 1
            pio device monitor --baud 115200 --port $board_port
        fi
    fi
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
# @option    --content <CONTENT>    Content to send
# @option    --ipv6 <IPV6>    IPv6 address to send to
test() {
    IFS="|" read board_env board_port board_serial < <(get_board)
    export BOARD_ENV=$board_env
    export BOARD_PORT=$board_port
    export BOARD_SERIAL=$board_serial
    if [ -z "$argc_ipv6" ]; then
        args_ipv6=$(gum input --placeholder="IPV6 to send, defaults to FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF")
        if [ -z "$args_ipv6" ]; then
            args_ipv6="FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF:FFFF"
        fi
    fi
    if [ -z "$args_content" ]; then
        args_content=$(gum input --placeholder="Sit perferendis nihil omnis. Accusantium voluptas asperiores dignissimos impedit quasi sint. Est laudantium dolorum alias sequi impedit reiciendis sed nostrum. Vitae consequuntur ad earum minima tempore labore. Nulla nesciunt quas culpa ullam. Eos suscipit perferendis repellendus. Rem tenetur in qui fuga ut. Quia quo consequatur modi quia impedit. Nisi porro ut officiis ducimus et id et. Illum quod earum tempore in aut corrupti aut. Eos doloribus ducimus voluptate vero consequatur debitis. Deserunt minima vel itaque sit aut reprehenderit nostrum. Consequuntur laborum optio maiores voluptatum eos occaecati rem.")
        if [ -z "$args_content" ]; then
            args_content="Sit perferendis nihil omnis. Accusantium voluptas asperiores dignissimos impedit quasi sint. Est laudantium dolorum alias sequi impedit reiciendis sed nostrum. Vitae consequuntur ad earum minima tempore labore. Nulla nesciunt quas culpa ullam. Eos suscipit perferendis repellendus. Rem tenetur in qui fuga ut. Quia quo consequatur modi quia impedit. Nisi porro ut officiis ducimus et id et. Illum quod earum tempore in aut corrupti aut. Eos doloribus ducimus voluptate vero consequatur debitis. Deserunt minima vel itaque sit aut reprehenderit nostrum. Consequuntur laborum optio maiores voluptatum eos occaecati rem."
        fi
    fi
    python3 script/send.py --port $board_port --address $args_ipv6  --payload "$args_content"
}

eval "$(argc --argc-eval "$0" "$@")"
