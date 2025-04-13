env := env_var("BOARD")
# port := shell('pio device list --json-output | jq -c ".[] | select(.hwid != \"n/a\")" | fzy | jq -r ".port"')

build:
    pio -f -c vim run -e {{env}}

upload:
    pio -f -c vim run -e {{env}} -t upload

update:
    pio project init --ide vim && pio run --target compiledb

serial port:
    pio device monitor --baud 115200 --port {{port}}

