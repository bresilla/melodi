# Melodi Mesh Network Examples

This directory contains Python examples demonstrating how to use the Melodi LoRa mesh network from PC applications. Each example is a standalone script that communicates with a Melodi LoRa node via serial interface.

## Architecture Overview

```
PC Application ←→ Serial ←→ LoRa Node ←→ Radio ←→ Mesh Network
```

The LoRa nodes act as **serial peripherals** connected to PCs. The PC sends commands via serial to the LoRa node, which then transmits over the mesh network. Received mesh messages are forwarded back to the PC via serial.

## Prerequisites

### Hardware Setup
1. **LoRa Node**: TTGO LoRa32, Adafruit Feather M0 LoRa, or Raspberry Pi with LoRa module
2. **USB Connection**: Connect LoRa node to PC via USB cable
3. **Firmware**: Flash the Melodi firmware to your LoRa node using PlatformIO

### Software Requirements
```bash
pip install pyserial
```

### Finding Your Serial Port
- **Linux**: Usually `/dev/ttyUSB0`, `/dev/ttyACM0`, or similar
- **Windows**: Usually `COM3`, `COM4`, etc.
- **macOS**: Usually `/dev/tty.usbserial-*` or `/dev/tty.usbmodem-*`

Use `python -m serial.tools.list_ports` to list available ports.

## Examples Overview

### 1. Basic Point-to-Point Demo (`basic_p2p_demo.py`)

Demonstrates direct communication between two specific nodes.

**Usage:**
```bash
# Node A (sends to Node B)
python basic_p2p_demo.py --port /dev/ttyUSB0 --dest 2001:db8::2 --name "Node-A"

# Node B (sends to Node A)  
python basic_p2p_demo.py --port /dev/ttyUSB1 --dest 2001:db8::1 --name "Node-B"
```

**Features:**
- Sends periodic "Hello" messages to specific IPv6 address
- Displays received messages with timestamps
- Shows message fragmentation for longer messages
- Configurable send interval and custom messages

**Example Output:**
```
=== Melodi Basic P2P Demo ===
Node Name: Node-A
Target: 2001:db8::2
Interval: 10s
========================================
Node IPv6: 2001:db8::1
Radio: Active
TX Power: 23 dBm
----------------------------------------
[14:32:15] TX to 2001:db8::2: Hello from Node-A - Message #1
[14:32:15] ✓ Message sent successfully
[14:32:18] RX DIRECT from 2001:db8::2: Hello from Node-B - Message #3
```

### 2. Broadcast Demo (`broadcast_demo.py`)

Demonstrates broadcast messaging to all nodes in the mesh network.

**Usage:**
```bash
python broadcast_demo.py --port /dev/ttyUSB0 --name "Node-A" --interval 15
```

**Features:**
- Sends broadcast messages to all mesh nodes (`ffff:ffff:...`)
- Automatic node discovery and tracking
- Network statistics and performance monitoring
- Shows mesh propagation and hop-based forwarding

**Example Output:**
```
=== Melodi broadcast Demo ===
Node Name: Node-A
Broadcast Interval: 15s
Target: ALL NODES (broadcast)
========================================
[14:35:20] TX BROADCAST: Broadcast announcement from Node-A #1
[14:35:20] ✓ Broadcast sent successfully
[14:35:22] *** NEW NODE DISCOVERED: 2001:db8::3 ***
[14:35:22] RX BROADCAST from 2001:db8::3: Network status from Node-C #5

--- Node-A Statistics ---
Messages Sent: 1
Messages Received: 1
  └─ Broadcast: 1
  └─ Direct: 0
```

### 3. Multi-hop Test Tool (`multihop_test.py`)

Tests routing performance and measures end-to-end latency through the mesh.

**Usage:**
```bash
python multihop_test.py --port /dev/ttyUSB0 --target 2001:db8::5 --name "TestNode-A"
```

**Features:**
- Measures round-trip latency through mesh hops
- Route performance statistics
- JSON-formatted test messages for precise timing
- Success rate calculation and latency analysis

**Example Output:**
```
=== Melodi Multi-hop Test Tool ===
Test Node: TestNode-A
Target: 2001:db8::5
========================================
[14:40:10.123] TX to 2001:db8::5: MSG_0001
[14:40:10.125] ✓ Message sent successfully
[14:40:12.847] ✓ RESPONSE from 2001:db8::5: MSG_0001 (latency: 2.724s)

--- Route Stats for 2001:db8::5 ---
Success Rate: 85.0% (17/20)
Average Latency: 2.156s
Min/Max Latency: 1.234s / 4.567s
```

### 4. Interactive Mesh Terminal (`interactive_mesh.py`)

Provides a command-line interface for real-time mesh network interaction.

**Usage:**
```bash
python interactive_mesh.py --port /dev/ttyUSB0 --name "MyNode"
```

**Available Commands:**
- `send <ipv6> <message>` - Send message to specific node
- `broadcast <message>` - Send broadcast message to all nodes  
- `ping <ipv6>` - Ping specific node with latency measurement
- `status` - Show node status and configuration
- `stats` - Display network statistics
- `nodes` - List all discovered nodes
- `help` - Show available commands
- `quit`/`exit` - Exit the terminal

**Example Session:**
```
=== Melodi Interactive Mesh Terminal ===
Node Name: MyNode
Port: /dev/ttyUSB0
========================================
Connected as: 2001:db8::1
Radio: Active

Type 'help' for available commands
Listening for mesh traffic...

> send 2001:db8::2 Hello from interactive terminal!
Sending to 2001:db8::2: Hello from interactive terminal!
Message sent successfully

> broadcast Network announcement from MyNode
Broadcasting: Network announcement from MyNode
Broadcast sent successfully

[14:42:15] RX DIRECT from 2001:db8::2: Thanks for the message!

> ping 2001:db8::3
Pinging 2001:db8::3...
Ping sent

[14:42:20] PING RESPONSE from 2001:db8::3: 1.234s

> nodes
=== Discovered Nodes (3) ===
2001:db8::2 - 5 msgs, last seen: 14:42:15
2001:db8::3 - 2 msgs, last seen: 14:42:20
2001:db8::4 - 1 msgs, last seen: 14:41:30
===============================
```

## Network Testing Scenarios

### Two-Node Setup
Deploy two nodes for basic connectivity testing:
```bash
# Terminal 1
python basic_p2p_demo.py --port /dev/ttyUSB0 --dest 2001:db8::2 --name "Alice"

# Terminal 2  
python basic_p2p_demo.py --port /dev/ttyUSB1 --dest 2001:db8::1 --name "Bob"
```

### Three-Node Mesh
Test multi-hop forwarding with intermediate relay:
```bash
# Node A (far left)
python broadcast_demo.py --port /dev/ttyUSB0 --name "NodeA"

# Node B (center - relay)
python interactive_mesh.py --port /dev/ttyUSB1 --name "RelayB"

# Node C (far right)
python multihop_test.py --port /dev/ttyUSB2 --target 2001:db8::1 --name "NodeC"
```

### Large Network Testing
For testing with 4+ nodes:
```bash
# Broadcast node for network announcements
python broadcast_demo.py --port /dev/ttyUSB0 --name "Beacon" --interval 30

# Interactive nodes for manual testing  
python interactive_mesh.py --port /dev/ttyUSB1 --name "Control-1"
python interactive_mesh.py --port /dev/ttyUSB2 --name "Control-2"

# Automated test nodes
python multihop_test.py --port /dev/ttyUSB3 --target ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff
```

## Protocol Details

### Enhanced Serial Protocol
The examples use the enhanced Melodi protocol with structured commands:

**Commands to Node:**
- `CMD_SEND_MESSAGE (0x01)`: Send mesh message
- `CMD_GET_STATUS (0x03)`: Get node status

**Responses from Node:**
- `RESP_ACK (0x80)`: Command acknowledged
- `RESP_STATUS (0x82)`: Status information
- `RESP_MESSAGE (0x83)`: Received mesh message

**Message Format:**
```
Outgoing: [CMD][2 bytes length][16 bytes dest IPv6][payload]
Incoming: [0xAA,0xBB,0xCC,0xDD][type][broadcast flag][16 bytes src][2 bytes len][payload]
```

### IPv6 Addressing
- **Unicast**: Specific node addresses (e.g., `2001:db8::1`)
- **Broadcast**: All nodes (`ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff`)
- **Auto-generated**: Based on node serial number or configured manually

## Troubleshooting

### Common Issues

**Serial Connection Fails:**
```bash
# Check available ports
python -m serial.tools.list_ports

# Check permissions (Linux)
sudo usermod -a -G dialout $USER
# Then logout/login
```

**No Mesh Traffic:**
- Verify LoRa nodes are flashed with Melodi firmware
- Check nodes are on same frequency (868 MHz default)
- Ensure nodes are within radio range
- Try increasing TX power in node configuration

**High Latency/Packet Loss:**
- Reduce message send interval
- Check for radio interference
- Verify hop limit settings
- Test with shorter messages first

**Node Not Responding:**
```bash
# Test with simple status check
python -c "
import serial
ser = serial.Serial('/dev/ttyUSB0', 115200)
ser.write(bytes([0x03]))  # CMD_GET_STATUS
print(ser.read(50))
"
```

### Debug Mode
Add debug output to any example by modifying the message callback:
```python
def on_message_received(src_ipv6, message, broadcast):
    print(f"DEBUG: Raw message from {src_ipv6}: {repr(message)}")
    # ... rest of function
```

## Performance Tips

1. **Optimize Send Intervals**: Start with 10-30 second intervals for testing
2. **Message Size**: Keep messages under 100 bytes for best performance  
3. **Network Size**: Test with 2-3 nodes first, then scale up
4. **Power Management**: Use appropriate TX power for your range requirements
5. **Frequency Planning**: Ensure all nodes use the same frequency

## Advanced Usage

### Custom Message Formats
Extend examples with JSON or custom protocols:
```python
import json

# Send structured data
data = {
    "sensor": "temperature", 
    "value": 23.5,
    "timestamp": time.time()
}
node.send_message(target, json.dumps(data))
```

### Integration with Other Systems
Examples can be easily integrated with:
- MQTT brokers for IoT integration
- Web APIs for remote monitoring  
- Databases for data logging
- Home automation systems

### Network Visualization
Use the discovered nodes data to create network topology graphs or web dashboards showing mesh connectivity and performance metrics.

---

## Contributing

Feel free to extend these examples with additional features:
- GPS location tracking
- Sensor data collection
- Web-based monitoring dashboard
- Mobile app integration
- Advanced routing protocols

Submit pull requests with new examples or improvements to existing ones!