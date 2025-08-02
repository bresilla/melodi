#!/usr/bin/env python3
"""
Debug Receiver - Shows raw bytes and parsing details
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

def bytes_to_ipv6(ipv6_bytes):
    """Convert 16 bytes to IPv6 string"""
    try:
        return socket.inet_ntop(socket.AF_INET6, ipv6_bytes)
    except socket.error:
        return None

def debug_receive(port):
    """Debug message reception"""
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        print(f"Connected to {port}")
        time.sleep(2)
        
        print("Listening for messages (debug mode)...")
        print("Press Ctrl+C to stop")
        print("-" * 50)
        
        buffer = b''
        
        while True:
            if ser.in_waiting > 0:
                new_data = ser.read(ser.in_waiting)
                buffer += new_data
                
                timestamp = datetime.now().strftime("%H:%M:%S")
                print(f"[{timestamp}] Raw bytes: {new_data.hex()}")
                
                # Look for header marker
                header_pos = buffer.find(HEADER_MARKER)
                if header_pos >= 0:
                    print(f"[{timestamp}] Found header at position {header_pos}")
                    buffer = buffer[header_pos:]
                    
                    if len(buffer) >= 5:
                        response_type = buffer[4]
                        print(f"[{timestamp}] Response type: 0x{response_type:02x}")
                        
                        if response_type == RESP_MESSAGE:
                            print(f"[{timestamp}] Processing message response...")
                            
                            if len(buffer) >= 5 + 1 + 16 + 2:
                                offset = 5
                                broadcast = bool(buffer[offset])
                                src_bytes = buffer[offset+1:offset+17]
                                msg_len_bytes = buffer[offset+17:offset+19]
                                msg_len = struct.unpack('>H', msg_len_bytes)[0]
                                
                                print(f"[{timestamp}] Broadcast: {broadcast}")
                                print(f"[{timestamp}] Source bytes: {src_bytes.hex()}")
                                print(f"[{timestamp}] Message length: {msg_len}")
                                
                                expected_total = 5 + 1 + 16 + 2 + msg_len
                                print(f"[{timestamp}] Expected total length: {expected_total}")
                                print(f"[{timestamp}] Current buffer length: {len(buffer)}")
                                
                                if len(buffer) >= expected_total:
                                    payload = buffer[offset+19:offset+19+msg_len]
                                    
                                    src_ipv6 = bytes_to_ipv6(src_bytes)
                                    message = payload.decode('utf-8', errors='ignore')
                                    
                                    print(f"[{timestamp}] Source IPv6: {src_ipv6}")
                                    print(f"[{timestamp}] Payload bytes: {payload.hex()}")
                                    print(f"[{timestamp}] Message: '{message}'")
                                    print(f"[{timestamp}] *** RECEIVED MESSAGE: '{message}' from {src_ipv6} ***")
                                    
                                    # Remove processed message from buffer
                                    buffer = buffer[expected_total:]
                                else:
                                    print(f"[{timestamp}] Waiting for more data...")
                            else:
                                print(f"[{timestamp}] Incomplete message header")
                        else:
                            print(f"[{timestamp}] Non-message response, skipping")
                            buffer = buffer[5:]
                
            time.sleep(0.01)
            
    except KeyboardInterrupt:
        print("\nShutting down...")
        ser.close()
    except Exception as e:
        print(f"Error: {e}")

def main():
    parser = argparse.ArgumentParser(description='Debug Message Receiver')
    parser.add_argument('--port', required=True, help='Serial port (e.g., /dev/ttyUSB1)')
    
    args = parser.parse_args()
    
    print("=== Debug Message Receiver ===")
    debug_receive(args.port)

if __name__ == "__main__":
    main()