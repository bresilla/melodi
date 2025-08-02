#!/usr/bin/env python3
"""
Debug Large Message - Test specific message size with detailed logging
"""

import serial
import struct
import socket
import time
import sys
import argparse

# Protocol constants
CMD_SEND_MESSAGE = 0x01
RESP_ACK = 0x80
RESP_NACK = 0x81
HEADER_MARKER = bytes([0xAA, 0xBB, 0xCC, 0xDD])
BROADCAST_ADDRESS = "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"

def ipv6_to_bytes(ipv6_str):
    """Convert IPv6 string to 16 bytes"""
    try:
        return socket.inet_pton(socket.AF_INET6, ipv6_str)
    except socket.error:
        return None

def test_specific_size(port, size):
    """Test sending a message of specific size with detailed logging"""
    print(f"=== Testing {size} bytes ===")
    
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        time.sleep(1)
        
        # Generate message
        message = f"Test {size}B: " + "X" * (size - len(f"Test {size}B: "))
        message = message[:size]  # Ensure exact size
        
        msg_bytes = message.encode('utf-8')
        msg_len = len(msg_bytes)
        
        print(f"Message length: {msg_len} bytes")
        print(f"Expected fragments: {(msg_len + 127) // 128}")
        
        # Convert broadcast address to bytes
        dest_bytes = ipv6_to_bytes(BROADCAST_ADDRESS)
        
        # Build command data: [2 bytes length][16 bytes dest][N bytes payload]
        data = struct.pack('>H', msg_len) + dest_bytes + msg_bytes
        total_command_size = 1 + len(data)  # CMD byte + data
        
        print(f"Total command size: {total_command_size} bytes")
        print(f"Command breakdown:")
        print(f"  - CMD_SEND_MESSAGE: 1 byte")
        print(f"  - Payload length: 2 bytes")
        print(f"  - IPv6 destination: 16 bytes")
        print(f"  - Message payload: {msg_len} bytes")
        print(f"  - Total: {total_command_size} bytes")
        
        # Send command
        packet = bytes([CMD_SEND_MESSAGE]) + data
        
        print(f"Sending command...")
        ser.write(packet)
        ser.flush()
        print(f"Command sent, waiting for response...")
        
        # Monitor all serial output
        start_time = time.time()
        buffer = b''
        text_buffer = ""
        
        while time.time() - start_time < 15:  # Longer timeout
            if ser.in_waiting > 0:
                new_data = ser.read(ser.in_waiting)
                buffer += new_data
                
                # Try to decode as text for debug output
                try:
                    text_data = new_data.decode('utf-8', errors='ignore')
                    text_buffer += text_data
                    
                    # Print debug lines as they come
                    while '\n' in text_buffer:
                        line, text_buffer = text_buffer.split('\n', 1)
                        if line.strip():
                            print(f"DEBUG: {line.strip()}")
                except:
                    pass
                
                # Look for header marker
                header_pos = buffer.find(HEADER_MARKER)
                if header_pos >= 0:
                    response_buffer = buffer[header_pos:]
                    if len(response_buffer) >= 6:
                        response_type = response_buffer[4]
                        if response_type == RESP_ACK:
                            print("✓ ACCEPTED - Message accepted by firmware")
                            ser.close()
                            return True
                        elif response_type == RESP_NACK:
                            error_code = response_buffer[5] if len(response_buffer) > 5 else 0
                            print(f"✗ REJECTED - Error code: {error_code} (0x{error_code:02x})")
                            ser.close()
                            return False
            
            time.sleep(0.1)
        
        print("⚠ TIMEOUT - No response received")
        ser.close()
        return False
        
    except Exception as e:
        print(f"✗ ERROR - {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Debug Large Message')
    parser.add_argument('--port', required=True, help='Serial port (e.g., /dev/ttyACM0)')
    parser.add_argument('--size', type=int, default=500, help='Message size to test')
    
    args = parser.parse_args()
    
    print("=== Debug Large Message Test ===")
    result = test_specific_size(args.port, args.size)
    
    if result:
        print("✅ Test PASSED")
    else:
        print("❌ Test FAILED")

if __name__ == "__main__":
    main()