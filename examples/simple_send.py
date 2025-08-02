#!/usr/bin/env python3
"""
Simple Message Sender - Test sending "hello" message
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

def send_hello(port):
    """Send a simple hello message"""
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        print(f"Connected to {port}")
        time.sleep(2)
        
        # Prepare message
        message = "hello"
        msg_bytes = message.encode('utf-8')
        msg_len = len(msg_bytes)
        
        print(f"Sending message: '{message}' ({msg_len} bytes)")
        
        # Convert broadcast address to bytes
        dest_bytes = ipv6_to_bytes(BROADCAST_ADDRESS)
        if not dest_bytes:
            print("Failed to convert broadcast address")
            return False
        
        # Build command data: [2 bytes length][16 bytes dest][N bytes payload]
        data = struct.pack('>H', msg_len) + dest_bytes + msg_bytes
        
        print(f"Command data length: {len(data)} bytes")
        print(f"Message length field: {msg_len}")
        print(f"Destination: {BROADCAST_ADDRESS}")
        print(f"Payload: {msg_bytes}")
        
        # Send command
        packet = bytes([CMD_SEND_MESSAGE]) + data
        print(f"Sending packet: {packet.hex()}")
        
        ser.write(packet)
        ser.flush()
        
        # Wait for response
        print("Waiting for response...")
        start_time = time.time()
        buffer = b''
        
        while time.time() - start_time < 5:
            if ser.in_waiting > 0:
                new_data = ser.read(ser.in_waiting)
                buffer += new_data
                print(f"Received: {new_data.hex()}")
                
                # Look for header marker
                header_pos = buffer.find(HEADER_MARKER)
                if header_pos >= 0:
                    buffer = buffer[header_pos:]
                    if len(buffer) >= 6:
                        response_type = buffer[4]
                        if response_type == RESP_ACK:
                            print("✓ Message sent successfully!")
                            ser.close()
                            return True
                        elif response_type == RESP_NACK:
                            error_code = buffer[5] if len(buffer) > 5 else 0
                            print(f"✗ Message send failed: error code {error_code}")
                            ser.close()
                            return False
            time.sleep(0.01)
        
        print("No response received")
        ser.close()
        return False
        
    except Exception as e:
        print(f"Error: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Simple Hello Sender')
    parser.add_argument('--port', required=True, help='Serial port (e.g., /dev/ttyUSB0)')
    
    args = parser.parse_args()
    
    print("=== Simple Hello Sender ===")
    success = send_hello(args.port)
    
    if success:
        print("Message sent successfully!")
    else:
        print("Failed to send message")

if __name__ == "__main__":
    main()