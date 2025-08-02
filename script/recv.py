#!/usr/bin/env python3
"""
Receive messages from LoRa mesh network
"""

import serial
import struct
import socket
import time
import sys
import argparse
from datetime import datetime

# Protocol constants
RESP_MESSAGE = 0x83
HEADER_MARKER = bytes([0xAA, 0xBB, 0xCC, 0xDD])
CMD_SET_CONFIG = 0x04
CONFIG_IPV6_ADDRESS = 0x04
RESP_ACK = 0x80
RESP_NACK = 0x81

def set_ipv6_address(ser, ipv6_address):
    """Set the node's IPv6 address using CMD_SET_CONFIG"""
    try:
        # Convert IPv6 string to 16 bytes
        ipv6_bytes = socket.inet_pton(socket.AF_INET6, ipv6_address)
    except socket.error:
        print(f"Error: Invalid IPv6 address '{ipv6_address}'")
        return False
    
    print(f"Setting node address to: {ipv6_address}")
    
    # Build command: CMD_SET_CONFIG + CONFIG_TYPE + IPv6_BYTES
    command = bytes([CMD_SET_CONFIG, CONFIG_IPV6_ADDRESS]) + ipv6_bytes
    
    # Send command (no header needed for commands TO the node)
    ser.write(command)
    ser.flush()
    
    # Wait for response
    buffer = b""
    start_time = time.time()
    timeout = 2.0
    
    while time.time() - start_time < timeout:
        if ser.in_waiting > 0:
            new_data = ser.read(ser.in_waiting)
            buffer += new_data
            
            # Look for header marker
            header_pos = buffer.find(HEADER_MARKER)
            if header_pos >= 0 and len(buffer) >= header_pos + 6:  # Header + response type + command
                resp_type = buffer[header_pos + 4]
                
                if resp_type == RESP_ACK:
                    print("✓ Node address set successfully")
                    return True
                elif resp_type == RESP_NACK:
                    if len(buffer) >= header_pos + 7:  # NACK includes error code
                        cmd = buffer[header_pos + 5]
                        error_code = buffer[header_pos + 6]
                        print(f"✗ NACK received for command 0x{cmd:02x}, error: 0x{error_code:02x}")
                    else:
                        print("✗ NACK received")
                    return False
        
        time.sleep(0.1)
    
    print("⚠ Warning: No ACK received for address setting")
    return False

def bytes_to_ipv6(ipv6_bytes):
    """Convert 16 bytes to IPv6 string"""
    try:
        return socket.inet_ntop(socket.AF_INET6, ipv6_bytes)
    except socket.error:
        # Fallback to hex representation
        return ":".join(f"{ipv6_bytes[i]:02x}{ipv6_bytes[i+1]:02x}" 
                       for i in range(0, 16, 2))

def receive_messages(port, debug=False, node_ip=None):
    """Receive messages with optional debug output"""
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        print(f"Connected to {port}")
        time.sleep(2)  # Allow device to initialize
        
        # Set node IP if provided
        if node_ip:
            if not set_ipv6_address(ser, node_ip):
                print("Failed to set node IP address")
                ser.close()
                return
        
        print("Listening for messages...")
        print("Press Ctrl+C to stop")
        print("-" * 60)
        
        buffer = b''
        
        while True:
            if ser.in_waiting > 0:
                new_data = ser.read(ser.in_waiting)
                buffer += new_data
                
                if debug:
                    timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    print(f"[{timestamp}] Raw bytes ({len(new_data)}): {new_data.hex()}")
                
                # Look for header marker
                while True:
                    header_pos = buffer.find(HEADER_MARKER)
                    if header_pos == -1:
                        # Keep last few bytes in case header is split
                        if len(buffer) > 4:
                            buffer = buffer[-4:]
                        break
                    
                    # Remove everything before header
                    buffer = buffer[header_pos:]
                    
                    if len(buffer) < 5:
                        break  # Not enough data for response type
                    
                    response_type = buffer[4]
                    
                    if response_type == RESP_MESSAGE:
                        # Message format: [header 4][type 1][broadcast 1][source 16][length 2][payload N]
                        min_msg_size = 5 + 1 + 16 + 2  # 24 bytes minimum
                        
                        if len(buffer) < min_msg_size:
                            break  # Wait for more data
                        
                        # Parse message header
                        offset = 5
                        broadcast = bool(buffer[offset])
                        src_bytes = buffer[offset+1:offset+17]
                        msg_len_bytes = buffer[offset+17:offset+19]
                        msg_len = struct.unpack('>H', msg_len_bytes)[0]
                        
                        expected_total = min_msg_size + msg_len
                        
                        if debug:
                            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                            print(f"[{timestamp}] Message header: broadcast={broadcast}, length={msg_len}")
                            print(f"[{timestamp}] Need {expected_total} bytes, have {len(buffer)}")
                        
                        if len(buffer) < expected_total:
                            break  # Wait for complete message
                        
                        # Extract payload
                        payload = buffer[offset+19:offset+19+msg_len]
                        
                        # Convert source to IPv6
                        src_ipv6 = bytes_to_ipv6(src_bytes)
                        
                        # Try to decode as text
                        try:
                            message = payload.decode('utf-8')
                        except UnicodeDecodeError:
                            message = f"<binary: {payload.hex()}>"
                        
                        # Display message
                        timestamp = datetime.now().strftime("%H:%M:%S")
                        msg_type = "BROADCAST" if broadcast else "DIRECT"
                        
                        print(f"\n[{timestamp}] {msg_type} MESSAGE")
                        print(f"From: {src_ipv6}")
                        print(f"Size: {msg_len} bytes")
                        print(f"Data: {message}")
                        print("-" * 60)
                        
                        # Remove processed message from buffer
                        buffer = buffer[expected_total:]
                    else:
                        # Skip other response types
                        if debug:
                            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                            print(f"[{timestamp}] Skipping response type 0x{response_type:02x}")
                        buffer = buffer[5:]
            
            time.sleep(0.01)
            
    except KeyboardInterrupt:
        print("\nShutting down...")
        ser.close()
    except serial.SerialException as e:
        print(f"Serial error: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

def main():
    parser = argparse.ArgumentParser(description='Receive messages from LoRa mesh network')
    parser.add_argument('--port', default='/dev/ttyACM0', 
                       help='Serial port (default: /dev/ttyACM0)')
    parser.add_argument('--debug', action='store_true',
                       help='Show debug output including raw bytes')
    parser.add_argument('--ip', help='Set node IPv6 address before receiving (e.g., 2001:db8::1)')
    
    args = parser.parse_args()
    
    receive_messages(args.port, args.debug, args.ip)

if __name__ == "__main__":
    main()