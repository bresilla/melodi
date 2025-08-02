#!/usr/bin/env python3
"""
Send messages via LoRa mesh network
"""

import serial
import struct
import socket
import time
import sys
import argparse

# Protocol constants
CMD_SEND_MESSAGE = 0x01
CMD_SET_CONFIG = 0x04
CONFIG_IPV6_ADDRESS = 0x04
RESP_ACK = 0x80
RESP_NACK = 0x81
HEADER_MARKER = bytes([0xAA, 0xBB, 0xCC, 0xDD])
BROADCAST_ADDRESS = "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"

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

def ipv6_to_bytes(ipv6_str):
    """Convert IPv6 string to 16 bytes"""
    try:
        return socket.inet_pton(socket.AF_INET6, ipv6_str)
    except socket.error:
        return None

def send_message(port, address, payload, timeout=15, node_ip=None):
    """Send a message with proper protocol handling"""
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        time.sleep(1)  # Allow device to initialize
        
        # Set node IP if provided
        if node_ip:
            if not set_ipv6_address(ser, node_ip):
                print("Failed to set node IP address")
                ser.close()
                return False
            # Close and reopen serial connection after IP change
            ser.close()
            time.sleep(0.5)
            ser = serial.Serial(port, 115200, timeout=1)
            time.sleep(1)  # Allow device to reinitialize
        
        # Convert address
        if address.lower() == "broadcast":
            dest_bytes = ipv6_to_bytes(BROADCAST_ADDRESS)
            print(f"Destination: BROADCAST")
        else:
            dest_bytes = ipv6_to_bytes(address)
            if not dest_bytes:
                print(f"Error: Invalid IPv6 address '{address}'")
                return False
            print(f"Destination: {address}")
        
        # Prepare message
        msg_bytes = payload.encode('utf-8')
        msg_len = len(msg_bytes)
        
        print(f"Message length: {msg_len} bytes")
        print(f"Expected fragments: {(msg_len + 127) // 128}")
        
        # Build command data: [2 bytes length][16 bytes dest][N bytes payload]
        data = struct.pack('>H', msg_len) + dest_bytes + msg_bytes
        total_command_size = 1 + len(data)  # CMD byte + data
        
        # Send command
        packet = bytes([CMD_SEND_MESSAGE]) + data
        
        print(f"Sending message...")
        ser.write(packet)
        ser.flush()
        
        # Wait for response
        start_time = time.time()
        buffer = b''
        text_buffer = ""
        
        while time.time() - start_time < timeout:
            if ser.in_waiting > 0:
                new_data = ser.read(ser.in_waiting)
                buffer += new_data
                
                # Try to decode debug output
                try:
                    text_data = new_data.decode('utf-8', errors='ignore')
                    text_buffer += text_data
                    
                    # Print debug lines
                    while '\n' in text_buffer:
                        line, text_buffer = text_buffer.split('\n', 1)
                        if line.strip():
                            print(f"DEBUG: {line.strip()}")
                except:
                    pass
                
                # Look for response
                header_pos = buffer.find(HEADER_MARKER)
                if header_pos >= 0:
                    response_buffer = buffer[header_pos:]
                    if len(response_buffer) >= 6:
                        response_type = response_buffer[4]
                        if response_type == RESP_ACK:
                            print("✓ Message sent successfully")
                            ser.close()
                            return True
                        elif response_type == RESP_NACK:
                            cmd = response_buffer[5] if len(response_buffer) > 5 else 0
                            error_code = response_buffer[6] if len(response_buffer) > 6 else 0
                            error_names = {
                                0x01: "INVALID_COMMAND",
                                0x02: "INVALID_IPV6", 
                                0x03: "RADIO_FAILURE",
                                0x04: "BUFFER_OVERFLOW",
                                0x05: "TIMEOUT",
                                0x06: "CHECKSUM_FAILED",
                                0x07: "MESSAGE_TOO_LARGE"
                            }
                            error_name = error_names.get(error_code, f"UNKNOWN(0x{error_code:02x})")
                            print(f"✗ Message rejected - Error: {error_name}")
                            ser.close()
                            return False
            
            time.sleep(0.1)
        
        print("⚠ Timeout - No response received")
        ser.close()
        return False
        
    except serial.SerialException as e:
        print(f"Serial error: {e}")
        return False
    except Exception as e:
        print(f"Error: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Send messages via LoRa mesh network')
    parser.add_argument('--port', required=True, help='Serial port (e.g., /dev/ttyACM0)')
    parser.add_argument('--address', required=True, 
                       help='Destination IPv6 address (e.g., 2001:db8::1) or "broadcast"')
    parser.add_argument('--payload', required=True, help='Message to send')
    parser.add_argument('--timeout', type=int, default=15, help='Response timeout in seconds (default: 15)')
    parser.add_argument('--ip', help='Set node IPv6 address before sending (e.g., 2001:db8::1)')
    
    args = parser.parse_args()
    
    success = send_message(args.port, args.address, args.payload, args.timeout, args.ip)
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()