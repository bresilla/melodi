SHELL := /bin/bash
PROJECT_NAME := melodi
TOP_DIR      := $(CURDIR)

# Device mapping for board detection
DEVICE_TTGO := 1A86:55D4
# DEVICE_M0 := 239A:800B
# DEVICE_RPI := 239A:80F1

# Default board environment
BOARD_ENV ?= ttgo

# Colors for output
GREEN := \033[0;32m
YELLOW := \033[0;33m
NC := \033[0m

# Check for required tools
REQUIRED_TOOLS := pio jq gum
CHECK_TOOLS := $(foreach tool,$(REQUIRED_TOOLS),$(if $(shell which $(tool) 2>/dev/null),$(tool),))
MISSING_TOOLS := $(filter-out $(CHECK_TOOLS),$(REQUIRED_TOOLS))

ifneq ($(MISSING_TOOLS),)
$(error Missing required tools: $(MISSING_TOOLS). Please install them first)
endif

$(info ------------------------------------------)
$(info Project: $(PROJECT_NAME))
$(info ------------------------------------------)

.PHONY: build b upload u refresh r monitor m send s recv g clean help h

# Get board information
define get_board
$(shell pio device list --json-output | jq -c '.[] | select(.hwid != "n/a")' | gum choose --limit=1 --select-if-one | jq -r '.port')
endef

define get_board_info
$(shell board=$$(pio device list --json-output | jq -c '.[] | select(.hwid != "n/a")' | gum choose --limit=1 --select-if-one); \
	echo "$$(echo "$$board" | jq -r '.port')|$$(echo "$$board" | jq -r '.hwid' | grep -oP 'VID:PID=\K[^ ]+' || echo 'unknown')")
endef

# Auto-detect board environment based on device ID
define detect_board_env
$(shell board=$$(pio device list --json-output | jq -c '.[] | select(.hwid != "n/a")' | gum choose --limit=1 --select-if-one); \
	hwid=$$(echo "$$board" | jq -r '.hwid' | grep -oP 'VID:PID=\K[^ ]+' || echo 'unknown'); \
	case "$$hwid" in \
		"1A86:55D4") echo "ttgo" ;; \
		"239A:800B") echo "m0" ;; \
		"239A:80F1") echo "rpi" ;; \
		*) echo "$(BOARD_ENV)" ;; \
	esac)
endef

build:
	@echo -e "$(GREEN)Building for $(BOARD_ENV)...$(NC)"
	@if [ -z "$(BOARD_ENV)" ]; then \
		echo "Building all boards..."; \
		pio -f -c vim run; \
	else \
		pio -f -c vim run -e $(BOARD_ENV); \
	fi

b: build

upload:
	@echo -e "$(GREEN)Detecting board...$(NC)"
	@board=$$(pio device list --json-output | jq -c '.[] | select(.hwid != "n/a")' | gum choose --limit=1 --select-if-one); \
	port=$$(echo "$$board" | jq -r '.port'); \
	hwid=$$(echo "$$board" | jq -r '.hwid' | grep -oP 'VID:PID=\K[^ ]+' || echo 'unknown'); \
	if [ -z "$$port" ]; then \
		echo -e "$(YELLOW)No board detected!$(NC)"; \
		exit 1; \
	fi; \
	case "$$hwid" in \
		"1A86:55D4") board_env="ttgo" ;; \
		"239A:800B") board_env="m0" ;; \
		"239A:80F1") board_env="rpi" ;; \
		*) board_env="unknown" ;; \
	esac; \
	echo -e "$(GREEN)Found $$board_env board at $$port ($$hwid)$(NC)"; \
	if [ "$$board_env" = "unknown" ]; then \
		echo -e "$(YELLOW)Unknown board type! Using default: $(BOARD_ENV)$(NC)"; \
		board_env="$(BOARD_ENV)"; \
	fi; \
	if [ -n "$(IPV6)" ]; then \
		export BOARD_IPV6=$(IPV6); \
		echo -e "$(GREEN)Setting IPv6: $(IPV6)$(NC)"; \
	fi; \
	if [ -z "$(NO_CONFIRM)" ]; then \
		gum confirm --default "Upload to $$board_env at $$port?" && pio -f -c vim run -e $$board_env -t upload --upload-port $$port; \
	else \
		pio -f -c vim run -e $$board_env -t upload --upload-port $$port; \
	fi; \
	if [ -n "$(MONITOR)" ]; then \
		sleep 1; \
		$(MAKE) monitor PORT=$$port; \
	fi

u: upload

refresh:
	@echo -e "$(GREEN)Refreshing project files...$(NC)"
	@pio project init --ide vim && pio run --target compiledb

r: refresh

monitor:
	@echo -e "$(GREEN)Starting serial monitor...$(NC)"
	@if [ -n "$(PORT)" ]; then \
		pio device monitor --baud 115200 --port $(PORT); \
	else \
		port=$$(pio device list --json-output | jq -c '.[] | select(.hwid != "n/a")' | gum choose --limit=1 --select-if-one | jq -r '.port'); \
		if [ -z "$$port" ]; then \
			echo -e "$(YELLOW)No board detected!$(NC)"; \
			exit 1; \
		fi; \
		echo -e "$(GREEN)Monitoring $$port...$(NC)"; \
		pio device monitor --baud 115200 --port $$port; \
	fi

m: monitor

send:
	@echo -e "$(GREEN)Sending message...$(NC)"
	@port=$$(pio device list --json-output | jq -c '.[] | select(.hwid != "n/a")' | gum choose --limit=1 --select-if-one | jq -r '.port'); \
	if [ -z "$$port" ]; then \
		echo -e "$(YELLOW)No board detected!$(NC)"; \
		exit 1; \
	fi; \
	ipv6="$(IPV6)"; \
	if [ -z "$$ipv6" ]; then \
		ipv6=$$(gum input --placeholder="IPv6 address (default: broadcast)"); \
		if [ -z "$$ipv6" ]; then \
			ipv6="broadcast"; \
		fi; \
	fi; \
	content="$(CONTENT)"; \
	if [ -z "$$content" ]; then \
		content=$$(gum input --placeholder="Message content (default: Hello LoRa)"); \
		if [ -z "$$content" ]; then \
			content="Hello from LoRa mesh network!"; \
		fi; \
	fi; \
	python3 script/send.py --port $$port --address "$$ipv6" --payload "$$content"

s: send

recv:
	@echo -e "$(GREEN)Receiving messages...$(NC)"
	@if [ -n "$(PORT)" ]; then \
		python3 script/recv.py --port $(PORT); \
	else \
		port=$$(pio device list --json-output | jq -c '.[] | select(.hwid != "n/a")' | gum choose --limit=1 --select-if-one | jq -r '.port'); \
		if [ -z "$$port" ]; then \
			echo -e "$(YELLOW)No board detected!$(NC)"; \
			exit 1; \
		fi; \
		echo -e "$(GREEN)Listening on $$port...$(NC)"; \
		python3 script/recv.py --port $$port; \
	fi

g: recv

clean:
	@echo -e "$(GREEN)Cleaning build files...$(NC)"
	@pio run --target clean
	@rm -rf .pio

help:
	@echo
	@echo "Usage: make [target] [OPTIONS]"
	@echo
	@echo "Available targets:"
	@echo "  build    (b)  - Build the firmware for BOARD_ENV"
	@echo "  upload   (u)  - Upload firmware to connected board"
	@echo "  refresh  (r)  - Refresh IDE project files"
	@echo "  monitor  (m)  - Monitor serial output"
	@echo "  send     (s)  - Send a test message"
	@echo "  recv     (g)  - Receive messages"
	@echo "  clean        - Clean build files"
	@echo "  help     (h)  - Show this help"
	@echo
	@echo "Options:"
	@echo "  BOARD_ENV=<env>    - Board environment (default: ttgo)"
	@echo "  IPV6=<address>     - IPv6 address for upload or send"
	@echo "  NO_CONFIRM=1       - Skip upload confirmation"
	@echo "  MONITOR=1          - Monitor after upload"
	@echo "  PORT=<port>        - Specific serial port"
	@echo "  CONTENT=<text>     - Message content for send"
	@echo
	@echo "Examples:"
	@echo "  make build"
	@echo "  make upload MONITOR=1"
	@echo "  make upload IPV6=2001:db8::1 NO_CONFIRM=1"
	@echo "  make send IPV6=2001:db8::2 CONTENT='Hello World'"
	@echo "  make monitor PORT=/dev/ttyACM0"
	@echo

h: help
