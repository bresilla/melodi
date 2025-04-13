env := env_var("BOARD")
board := 'pio device list --json-output | jq -c ".[] | select(.hwid != \"n/a\")" | fzy'
port := shell(board + ' | jq -r ".port"')
# serial := shell(board + ' | jq -r ".hwid" | cut -d" " -f3 | cut -d"=" -f2')

build:
    pio -f -c vim run -e {{env}}

upload:
    pio -f -c vim run -e {{env}} -t upload --upload-port {{port}}

update:
    pio project init --ide vim && pio run --target compiledb

serial:
    pio device monitor --baud 115200 --port {{port}}

