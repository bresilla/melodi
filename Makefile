
build:
	@pio -f -c vim run -e $(env)

upload:
	@pio -f -c vim run -e $(env) -t upload

update:
	@pio project init --ide vim && pio run --target compiledb

serial:
	@pio device monitor --baud 115200
