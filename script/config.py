#!/usr/bin/env python3
import serial
import struct
import argparse
import socket
import time

# Protocol constants
HEADER_MARKER = b"\xaa\xbb\xcc\xdd"
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

def main():
    parser = argparse.ArgumentParser(description="Configure LoRa mesh node")
    parser.add_argument("--port", default="/dev/ttyACM0", help="Serial port (default: /dev/ttyACM0)")
    parser.add_argument("--address", required=True, help="IPv6 address to set (e.g., 2001:db8::1)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    args = parser.parse_args()
    
    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
        time.sleep(1)  # Give device time to initialize
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}")
        return 1
    
    try:
        if set_ipv6_address(ser, args.address):
            return 0
        else:
            return 1
    finally:
        ser.close()

if __name__ == "__main__":
    exit(main())
