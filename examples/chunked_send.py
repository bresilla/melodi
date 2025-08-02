#!/usr/bin/env python3
"""
Chunked Send - Send large messages in chunks to avoid serial buffer overflow
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

def send_chunked_message(port, size, chunk_size=64):
    """Send message in chunks to avoid serial buffer overflow"""
    print(f"=== Testing {size} bytes with {chunk_size}-byte chunks ===")
    
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        time.sleep(1)
        
        # Generate message
        message = f"Chunked {size}B: " + "X" * (size - len(f"Chunked {size}B: "))
        message = message[:size]
        
        msg_bytes = message.encode('utf-8')
        msg_len = len(msg_bytes)
        
        # Build command data
        dest_bytes = ipv6_to_bytes(BROADCAST_ADDRESS)
        data = struct.pack('>H', msg_len) + dest_bytes + msg_bytes
        packet = bytes([CMD_SEND_MESSAGE]) + data
        
        print(f"Sending {len(packet)} bytes in {chunk_size}-byte chunks...")
        
        # Send in chunks
        for i in range(0, len(packet), chunk_size):
            chunk = packet[i:i+chunk_size]
            print(f"Sending chunk {i//chunk_size + 1}: {len(chunk)} bytes")
            ser.write(chunk)
            ser.flush()
            time.sleep(0.01)  # Small delay between chunks
        
        print("All chunks sent, waiting for response...")
        
        # Wait for response
        start_time = time.time()
        buffer = b''
        
        while time.time() - start_time < 10:
            if ser.in_waiting > 0:
                new_data = ser.read(ser.in_waiting)
                buffer += new_data
                
                # Print debug output
                try:
                    text_data = new_data.decode('utf-8', errors='ignore')
                    for line in text_data.split('\n'):
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
                            print("✓ ACCEPTED")
                            ser.close()
                            return True
                        elif response_type == RESP_NACK:
                            error_code = response_buffer[5]
                            print(f"✗ REJECTED - Error: {error_code}")
                            ser.close()
                            return False
            
            time.sleep(0.1)
        
        print("⚠ TIMEOUT")
        ser.close()
        return False
        
    except Exception as e:
        print(f"✗ ERROR - {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Chunked Send Test')
    parser.add_argument('--port', required=True, help='Serial port')
    parser.add_argument('--size', type=int, default=500, help='Message size')
    parser.add_argument('--chunk', type=int, default=64, help='Chunk size')
    
    args = parser.parse_args()
    
    result = send_chunked_message(args.port, args.size, args.chunk)
    print("✅ SUCCESS" if result else "❌ FAILED")

if __name__ == "__main__":
    main()