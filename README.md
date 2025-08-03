<img align="right" width="26%" src="./misc/logo.png">

Melodi (**ME**sh **LO**ra **DI**stributed)
===

This project implements an IPv6-based mesh network over LoRa radio using the RadioHead RH_RF95 library. The primary goal is to enable nodes to communicate using IPv6 packets, even though LoRa's native packet size is limited. To overcome these constraints, the protocol implements fragmentation, reassembly, duplicate detection, and a configurable hop-based forwarding mechanism.

---

## Table of Contents

- [Project Structure](#project-structure)
- [Protocol Overview](#protocol-overview)
  - [IPv6 Addressing](#ipv6-addressing)
  - [Packet Structure and Fragmentation](#packet-structure-and-fragmentation)
  - [Hopping and Forwarding Mechanism](#hopping-and-forwarding-mechanism)
  - [Reassembly Process](#reassembly-process)
  - [Duplicate Detection](#duplicate-detection)
- [Serial Interface](#serial-interface)
  - [Input Format](#input-format)
  - [Output Format](#output-format)
  - [Configuration Commands](#configuration-commands)
- [Python Scripts](#python-scripts)
  - [send.py Usage](#sendpy-usage)
  - [recv.py Usage](#recvpy-usage)
- [Files and Key Functionality](#files-and-key-functionality)
- [Build and Deployment](#build-and-deployment)
- [Additional Notes](#additional-notes)
- [License](#license)

---

## Project Structure

The project files are organized as follows:
```
./
├── include/
│   ├── mesh.h       # Protocol definitions, packet structures, and reassembly contexts.
│   └── node.h       # Node-specific definitions and board-dependent pin configurations.
├── script/
│   ├── build.py     # Pre-build script to generate IPv6 address from serial or provided IPv6.
│   ├── recv.py      # Python script to receive and display messages with configuration support.
│   └── send.py      # Python script to send messages with advanced configuration options.
├── src/
│   ├── main.cpp     # Main application loop: initializes the node and polls for incoming messages.
│   ├── mesh.cpp     # Implementation of the protocol: fragmentation, reassembly, duplicate detection, and packet forwarding.
│   └── node.cpp     # Node implementation: radio initialization, serial communication, and message dispatch.
├── platformio.ini   # Build configuration for various boards (TTGO, Feather M0, Raspberry Pi).
└── run.sh           # Bash script providing a CLI to build, upload, and interact with the node.
```

Each directory is dedicated to different aspects:  
- **include/** contains header files with key structures and functions.  
- **src/** houses the primary implementation.  
- **script/** provides tools for building and testing via serial.  
- **platformio.ini** and **run.sh** facilitate board configuration and device interaction.

---

## Protocol Overview

The communication protocol is designed to support IPv6 messages over LoRa, compensating for the physical layer's constraints by splitting messages into smaller fragments, handling hop forwarding, preventing duplicates, and reassembling incoming message fragments.

### IPv6 Addressing

Each node holds a unique 128-bit address formed from two 64-bit integers. The function `toIPv6Address()` splits these values into 16 individual bytes. Special addresses include:
- **Broadcast Address:** All 16 bytes set to `0xFF` (used to send a message to all nodes).
- **Ignore Address:** All 16 bytes set to `0x00` (packets directed here are silently ignored).

### Packet Structure and Fragmentation

To overcome the limitations of LoRa's maximum message size, messages are fragmented into smaller chunks. The key structures are:

- **FragInfo Structure (16 bits):**  
  - **packetID (4 bits):** A unique identifier for a message (cycles from 0 to 15).  
  - **hopLimit (4 bits):** Time-to-live for the packet. Initially set to 3 (configurable 1-15).  
  - **fragmentIndex (4 bits):** The index of the fragment within the message (starting at 0).  
  - **fragmentTotal (4 bits):** Total number of fragments that make up the complete message.

- **IPv6Packet Structure:**  
  This structure holds the following fields:  
  - **FragInfo:** Contains fragmentation and routing information.  
  - **payloadLength (8 bits):** Indicates the length of the payload in this fragment.  
  - **Source Address:** 16 bytes identifying the sender.  
  - **Destination Address:** 16 bytes for the intended recipient, which could be a specific node or broadcast.  
  - **Payload:** A fragment of the overall message with a fixed maximum size (`MAX_PAYLOAD_SIZE`).  

- **Message Fragmentation:**  
  - When sending, the message is split into chunks.  
  - Each fragment is encapsulated into an IPv6Packet.  
  - If the overall message exceeds the total capacity (controlled via `MAX_FRAGMENTS`), it may be truncated.  
  - Each fragment is transmitted repeatedly (as dictated by `repeatCount`, configurable 1-255) to mitigate potential packet loss.

### Hopping and Forwarding Mechanism

- **Hop Limit and Decrement:**  
  The hop limit (in the FragInfo structure) is used to control how many nodes a packet may traverse.  
  - For every forwarding action (in `forwardPacket()`), the hop limit is decremented.  
  - Packets with a hop limit of 0 are no longer forwarded, preventing endless routing loops.
  - Default hop limit is 3, configurable from 1-15 via serial commands.

- **Node Behavior:**  
  When a node receives a packet:
  - **If the destination matches its own IPv6 address** or if the destination is the **broadcast address**, the node processes and attempts to reassemble the message.
  - **If the destination does not match** (and is not an ignore address), the node decrements the hop limit and forwards the packet to extend the network's reach.

### Reassembly Process

Incoming fragments are stored in a *reassembly context*:
- **Reassembly Context:**  
  - Maintains a buffer (`dataBuffer`) to store fragments.
  - Records which fragments have been received using an array (`fragmentsReceived`).
  - Tracks the total number of fragments (`fragmentTotal`) and the length of the final fragment (`lastFragmentLength`).
  - Each context is timestamped and timed out if no new fragments are received within 30 seconds (`REASSEMBLY_TIMEOUT`).

- **Completing Reassembly:**  
  - The `getCompletedContext()` function checks if all expected fragments are present.
  - Once complete, the individual fragments are concatenated into a single message (with a total length derived from the number of full fragments plus the final fragment's length).
  - The reassembled packet is then forwarded over the serial interface.

- **Timeout Handling:**  
  - The function `deleteOldContexts()` periodically purges contexts where fragments have not been received in time.

### Duplicate Detection

To prevent duplicate messages in mesh networks where the same message may arrive via multiple paths, the system implements hash-based duplicate detection:

- **Message Hashing:** When a message is fully reassembled, a hash is calculated using the djb2 algorithm on the message content plus source address.
- **Recent Messages Cache:** Maintains a circular buffer of the last 20 message hashes with timestamps.
- **Duplicate Prevention:** Messages with the same hash from the same source within 5 seconds are detected as duplicates and silently discarded.
- **Automatic Cleanup:** Expired entries (older than 5 seconds) are automatically cleaned up to prevent memory leaks.

This ensures that when using repeat counts or when messages traverse multiple mesh paths, only the first copy is displayed to the user.

---

## Serial Interface

The system interacts with a host computer over serial communication for both input and output. There are multiple channels for different purposes:

### Input Format

Data received from the serial port is used to trigger message transmissions. The expected input format is:

**For CMD_SEND_MESSAGE (0x01):**
1. **Command byte:** 0x01
2. **Payload Length (2 bytes):** Message length (unsigned, big-endian)
3. **Repeat Count (1 byte):** Number of times to repeat each fragment (1-255)
4. **Destination IPv6 Address (16 bytes):** Target node's address
5. **Payload (N bytes):** The actual message content

The function `readSerialBinary()` in `node.cpp` handles this process, transitioning through states to read all components sequentially.

### Output Format

When a complete message is reassembled from incoming fragments:
- The system prepends a special header marker (`0xAA, 0xBB, 0xCC, 0xDD`) before sending the data over Serial.
- The reassembled packet is structured as follows:
  - **Broadcast flag (1 byte)**
  - **Source IPv6 Address (16 bytes)**
  - **Payload Length (2 bytes)**
  - **Payload Data (N bytes)**  
- This allows external tools (like `script/recv.py`) to correctly interpret and display the message.

### Configuration Commands

The firmware supports runtime configuration via CMD_SET_CONFIG (0x04):

**Set IPv6 Address (CONFIG_IPV6_ADDRESS = 0x04):**
- Command: `[0x04][0x04][16 bytes IPv6]`
- Sets the node's IPv6 address

**Set Hop Limit (CONFIG_HOP_LIMIT = 0x03):**
- Command: `[0x04][0x03][1 byte hop limit]`
- Sets the hop limit (1-15) for outgoing packets

**Set TX Power (CONFIG_TX_POWER = 0x01):**
- Command: `[0x04][0x01][1 byte power]`
- Sets the radio transmission power

**Set Frequency (CONFIG_FREQUENCY = 0x02):**
- Command: `[0x04][0x02][4 bytes frequency]`
- Sets the radio frequency

All configuration commands return ACK (0x80) on success or NACK (0x81) with error code on failure.

---

## Python Scripts

### send.py Usage

The `send.py` script provides a comprehensive interface for sending messages with full configuration control:

```bash
# Basic usage
python script/send.py --port /dev/ttyACM0 --address broadcast --payload "Hello, Mesh!"

# Set node IP address before sending
python script/send.py --port /dev/ttyACM0 --ip "2001:dead:beef::10" --address broadcast --payload "Hello"

# Control repeat count (fragment reliability)
python script/send.py --port /dev/ttyACM0 --address broadcast --payload "Hello" --repeat 3

# Set hop limit (mesh network reach)
python script/send.py --port /dev/ttyACM0 --address broadcast --payload "Hello" --hop-limit 5

# Send to specific node
python script/send.py --port /dev/ttyACM0 --address "2001:dead:beef::11" --payload "Direct message"

# Full configuration example
python script/send.py --port /dev/ttyACM0 --ip "2001:dead:beef::10" --address broadcast --payload "Configured message" --repeat 2 --hop-limit 4 --timeout 20
```

**Parameters:**
- `--port`: Serial port (required)
- `--address`: Destination IPv6 or "broadcast" (required)
- `--payload`: Message content (required)
- `--ip`: Set node IPv6 address before sending
- `--repeat`: Fragment repeat count (1-255, default: 1)
- `--hop-limit`: Hop limit (1-15, default: 3)
- `--timeout`: Response timeout in seconds (default: 15)

### recv.py Usage

The `recv.py` script listens for incoming messages with optional configuration and debugging:

```bash
# Basic usage
python script/recv.py --port /dev/ttyACM0

# Set node IP address for receiving
python script/recv.py --port /dev/ttyACM0 --ip "2001:dead:beef::11"

# Enable debug mode for troubleshooting
python script/recv.py --port /dev/ttyACM0 --debug

# Use different serial port
python script/recv.py --port /dev/ttyACM1 --ip "2001:dead:beef::12"
```

**Parameters:**
- `--port`: Serial port (default: /dev/ttyACM0)
- `--ip`: Set node IPv6 address before receiving
- `--debug`: Show debug output including raw bytes

**Output Format:**
```
[12:34:56] BROADCAST MESSAGE
From: 2001:dead:beef::10
Size: 13 bytes
Data: Hello, Mesh!
------------------------------------------------------------
```

---

## Files and Key Functionality

### `include/mesh.h`
- Defines the packet structures (`IPv6Packet`, `FragInfo`, `ReassemblyContext`, `ReassembledPacket`, `RecentMessage`).
- Declares protocol-specific functions for sending messages, forwarding packets, reassembling fragments, duplicate detection, and printing debug messages.

### `include/node.h`
- Contains definitions for board-specific configurations.
- Declares the `Node` class interface which wraps radio control and serial interactions.

### `src/mesh.cpp`
- Implements the core protocol logic:
  - **sendIPv6Message():** Splits messages into fragments, sets FragInfo fields, and sends each fragment with configurable repeat count.
  - **forwardPacket():** Decrements hop limit and re-forwards packets if not destined for the current node.
  - **createOrUpdateContext():** Manages reassembly contexts, storing fragments until a message is fully reconstructed.
  - **deleteOldContexts() and getCompletedContext():** Clean up old contexts and output complete messages.
  - **Duplicate detection functions:** Calculate hashes, track recent messages, and prevent duplicate serial output.
  
### `src/node.cpp`
- Implements node-level behavior:
  - **init():** Initializes Serial communication, resets and configures the LoRa radio, and computes the node's IPv6 address.
  - **poll():** Listens on the radio for incoming packets, processes received messages, and triggers reassembly or forwarding as needed.
  - **readSerialBinary():** Parses binary commands from the serial port to trigger outbound messages with repeat count support.
  - **Configuration handling:** Processes CMD_SET_CONFIG commands for IPv6, hop limit, TX power, and frequency.
  
### `script/build.py`
- A pre-build script that converts the provided IPv6 or board serial into a valid 128-bit IPv6 address for the node.

### `script/send.py` & `script/recv.py`
- **send.py:** Provides a CLI to send custom payloads with advanced configuration options (IP address, repeat count, hop limit).
- **recv.py:** Listens on the serial port, detects the header marker, unpacks reassembled messages, and supports IP configuration and debug modes.

### `platformio.ini`
- Contains build configuration for different target boards (such as TTGO, Feather M0, Raspberry Pi with LoRa).
- Sets board-specific flags, includes libraries, and integrates the build script.

### `run.sh`
- A command-line shell script offering commands to build, upload, refresh project files, monitor the serial port, and test sending/receiving messages.  
- Integrates tools such as `argc`, `pio`, `jq`, and `gum` to provide a user-friendly interface.

---

## Build and Deployment

### Pre-requisites

- **PlatformIO:** Ensure PlatformIO is installed for building the project.
- **argc, jq, gum:** These CLI utilities are used by `run.sh` for board detection and user interaction.
- **Serial Drivers:** Proper drivers must be installed to communicate with your chosen board.

### Building

You can build the project for all boards or a specific board by setting the environment variable `BOARD_ENV`. For example:
```bash
./run.sh build
```
or to build for a specific board:
```bash
BOARD_ENV=ttgo ./run.sh build
```
### Uploading 

Before uploading:

- Verify the target board is connected.
- Optionally provide an IPv6 address if you want to override the default node address.

To upload, run:

```bash
./run.sh upload --ipv6 2001:db8::1
```

You will be asked to confirm (unless the --no-confirm flag is specified).

### Monitoring and Testing

- Serial Monitor:

```bash
./run.sh monitor
```
- Testing Send/Receive:

To send a message via serial:

```bash
./run.sh send --ipv6 2001:db8::1 --content "Hello, Mesh!"
```

To receive and print reassembled packets:

```bash
./run.sh get
```

---

## Additional Notes

- **Fragmentation Considerations:**
    The current design splits outgoing messages into fixed-size fragments. If a message exceeds the maximum supported size, it may be truncated. Future enhancements could include error correction and better flow control.

- **Hop Limit and Routing:**
    The hop limit (set initially to 3, configurable 1-15) prevents packets from circulating indefinitely. Each intermediate node decrements this value, ensuring that packets eventually expire. This simple mechanism can be expanded with more intelligent routing rules if necessary.

- **Reassembly Timeout:**
    The project uses a 30-second timeout to clear incomplete message contexts. This helps free memory in case of lost fragments or delays in transmission.

- **Serial Communication Robustness:**
    The serial input parser (readSerialBinary()) follows a state machine approach to reliably extract length, repeat count, destination, and payload bytes. Any deviation (e.g., payload length mismatch) resets the state to ensure integrity.

- **Radio Reliability:**
    Each fragment is transmitted a number of times (as determined by repeatCount, configurable 1-255) to increase the chances of successful delivery over the LoRa link, which is inherently subject to interference and packet loss.

- **Duplicate Prevention:**
    The firmware implements hash-based duplicate detection to prevent the same message from being displayed multiple times when it arrives via different mesh paths or due to repeat transmissions. This ensures clean output in complex mesh topologies.

- **Configuration Flexibility:**
    Runtime configuration via serial commands allows dynamic adjustment of node parameters without reflashing firmware, making the system adaptable to different deployment scenarios.