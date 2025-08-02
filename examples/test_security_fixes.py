#!/usr/bin/env python3
"""
Test Security Fixes - Verify that buffer overflow protections work
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

def test_message_size(port, size, description):
    """Test sending a message of specific size"""
    print(f"\n=== Testing {description} ({size} bytes) ===")
    
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        time.sleep(1)
        
        # Generate message
        message = f"Test message of {size} bytes: " + "A" * (size - len(f"Test message of {size} bytes: "))
        message = message[:size]  # Ensure exact size
        
        msg_bytes = message.encode('utf-8')
        msg_len = len(msg_bytes)
        
        print(f"Sending {msg_len} bytes...")
        
        # Convert broadcast address to bytes
        dest_bytes = ipv6_to_bytes(BROADCAST_ADDRESS)
        if not dest_bytes:
            print("Failed to convert broadcast address")
            return False
        
        # Build command data: [2 bytes length][16 bytes dest][N bytes payload]
        data = struct.pack('>H', msg_len) + dest_bytes + msg_bytes
        
        # Send command
        packet = bytes([CMD_SEND_MESSAGE]) + data
        
        ser.write(packet)
        ser.flush()
        
        # Wait for response
        start_time = time.time()
        buffer = b''
        
        while time.time() - start_time < 5:
            if ser.in_waiting > 0:
                new_data = ser.read(ser.in_waiting)
                buffer += new_data
                
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
                            if error_code == 0x07:
                                print("  → ERR_MESSAGE_TOO_LARGE (expected for large messages)")
                            elif error_code == 0x04:
                                print("  → ERR_BUFFER_OVERFLOW (expected for very large messages)")
                            elif error_code == 0x01:
                                print("  → ERR_INVALID_COMMAND")
                            ser.close()
                            return False
            time.sleep(0.01)
        
        print("⚠ TIMEOUT - No response received")
        ser.close()
        return False
        
    except Exception as e:
        print(f"✗ ERROR - {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description='Test Security Fixes')
    parser.add_argument('--port', required=True, help='Serial port (e.g., /dev/ttyACM0)')
    
    args = parser.parse_args()
    
    print("=== Security Fixes Test Suite ===")
    print("Testing various message sizes to verify buffer overflow protection")
    
    # Test cases: (size, description, expected_result)
    test_cases = [
        (50, "Small message (should work)", True),
        (128, "Single fragment (should work)", True),
        (256, "Two fragments (should work)", True),
        (500, "Multiple fragments (should work)", True),
        (1000, "Large message (should work)", True),
        (2000, "Near limit (should work)", True),
        (2048, "At LoRa limit (should work)", True),
        (2049, "Over LoRa limit (should be rejected)", False),
        (3000, "Way over limit (should be rejected)", False),
        (5000, "Very large (should be rejected)", False),
    ]
    
    results = []
    
    for size, description, expected in test_cases:
        result = test_message_size(args.port, size, description)
        results.append((size, description, expected, result))
        
        # Small delay between tests
        time.sleep(0.5)
    
    # Summary
    print("\n" + "="*60)
    print("TEST RESULTS SUMMARY")
    print("="*60)
    
    passed = 0
    failed = 0
    
    for size, description, expected, actual in results:
        status = "PASS" if (expected == actual) else "FAIL"
        if status == "PASS":
            passed += 1
        else:
            failed += 1
            
        expected_str = "ACCEPT" if expected else "REJECT"
        actual_str = "ACCEPT" if actual else "REJECT"
        
        print(f"{size:4d} bytes | {status:4s} | Expected: {expected_str:6s} | Actual: {actual_str:6s} | {description}")
    
    print("="*60)
    print(f"PASSED: {passed}/{len(test_cases)} tests")
    print(f"FAILED: {failed}/{len(test_cases)} tests")
    
    if failed == 0:
        print("🎉 ALL TESTS PASSED - Security fixes are working correctly!")
    else:
        print("⚠️  SOME TESTS FAILED - Check firmware implementation")
    
    return failed == 0

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)