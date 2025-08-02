#!/usr/bin/env python3
"""
Big Message Test - Test sending larger messages that require fragmentation
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

def send_big_message(port, message):
    """Send a big message"""
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        print(f"Connected to {port}")
        time.sleep(2)
        
        # Prepare message
        msg_bytes = message.encode('utf-8')
        msg_len = len(msg_bytes)
        
        print(f"Sending message: '{message[:50]}{'...' if len(message) > 50 else ''}' ({msg_len} bytes)")
        
        # Convert broadcast address to bytes
        dest_bytes = ipv6_to_bytes(BROADCAST_ADDRESS)
        if not dest_bytes:
            print("Failed to convert broadcast address")
            return False
        
        # Build command data: [2 bytes length][16 bytes dest][N bytes payload]
        data = struct.pack('>H', msg_len) + dest_bytes + msg_bytes
        
        print(f"Command data length: {len(data)} bytes")
        print(f"Message length field: {msg_len}")
        expected_fragments = (msg_len + 127) // 128  # MAX_PAYLOAD_SIZE = 128
        print(f"Expected fragments: {expected_fragments}")
        
        # Send command
        packet = bytes([CMD_SEND_MESSAGE]) + data
        
        print("Sending command...")
        ser.write(packet)
        ser.flush()
        
        # For large messages, don't wait for immediate ACK
        # Instead, monitor serial output for transmission progress
        print("Monitoring transmission progress...")
        start_time = time.time()
        buffer = b''
        fragments_sent = 0
        
        # Calculate expected transmission time (rough estimate)
        # Each fragment takes time to transmit over LoRa
        expected_time = expected_fragments * 2  # 2 seconds per fragment estimate
        timeout = max(10, expected_time + 5)  # At least 10 seconds, plus buffer
        
        print(f"Estimated transmission time: {expected_time}s, timeout: {timeout}s")
        
        while time.time() - start_time < timeout:
            if ser.in_waiting > 0:
                new_data = ser.read(ser.in_waiting)
                buffer += new_data
                
                # Look for debug output indicating fragment transmission
                text_data = new_data.decode('utf-8', errors='ignore')
                if 'Sending binary fragment' in text_data:
                    fragments_sent += 1
                    print(f"Fragment {fragments_sent}/{expected_fragments} sent")
                
                # Look for completion messages
                if 'Message sent successfully' in text_data or 'Transmission complete' in text_data:
                    print("✓ All fragments transmitted!")
                    ser.close()
                    return True
                
                # Look for header marker for ACK/NACK
                header_pos = buffer.find(HEADER_MARKER)
                if header_pos >= 0:
                    response_buffer = buffer[header_pos:]
                    if len(response_buffer) >= 6:
                        response_type = response_buffer[4]
                        if response_type == RESP_ACK:
                            print("✓ Command accepted, fragments being transmitted...")
                            # Don't return here, continue monitoring
                        elif response_type == RESP_NACK:
                            error_code = response_buffer[5] if len(response_buffer) > 5 else 0
                            print(f"✗ Command rejected: error code {error_code}")
                            ser.close()
                            return False
            
            time.sleep(0.1)
        
        print(f"Transmission monitoring completed. Fragments sent: {fragments_sent}/{expected_fragments}")
        ser.close()
        
        # Consider it successful if we sent some fragments
        return fragments_sent > 0
        
    except Exception as e:
        print(f"Error: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Big Message Test')
    parser.add_argument('--port', required=True, help='Serial port (e.g., /dev/ttyACM0)')
    parser.add_argument('--size', type=int, default=200, help='Message size in bytes')
    parser.add_argument('--custom', help='Custom message to send')
    
    args = parser.parse_args()
    
    print("=== Big Message Test ===")
    
    if args.custom:
        message = args.custom
    else:
        # Generate a message of specified size
        base_msg = "This is a test message for fragmentation. "
        repeat_count = (args.size // len(base_msg)) + 1
        message = (base_msg * repeat_count)[:args.size]
        message += f" [END-{len(message)}]"
    
    print(f"Message size: {len(message)} bytes")
    print(f"First 100 chars: {message[:100]}")
    
    success = send_big_message(args.port, message)
    
    if success:
        print("Big message transmission initiated successfully!")
        print("Check the receiver to see if all fragments are received and reassembled.")
    else:
        print("Failed to send big message")

if __name__ == "__main__":
    main()