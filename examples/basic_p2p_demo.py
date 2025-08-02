#!/usr/bin/env python3
"""
Basic Point-to-Point Mesh Demo

This script demonstrates basic point-to-point communication over the Melodi LoRa mesh network.
The PC sends messages via serial to the connected LoRa node, which transmits them over the 
mesh network to a specific destination IPv6 address.

Usage:
    python basic_p2p_demo.py --port /dev/ttyUSB0 --dest 2001:db8::2 --name "Node-A"
    
Hardware Setup:
    - Connect LoRa node (TTGO/Feather/etc.) to PC via USB
    - Flash the Melodi firmware to the LoRa node
    - Run this script on multiple PCs with different destination addresses
"""

import serial
import struct
import socket
import time
import sys
import argparse
import threading
from datetime import datetime

# Protocol constants (matching node.h)
CMD_SEND_MESSAGE = 0x01
CMD_SET_IPV6 = 0x02
CMD_GET_STATUS = 0x03

RESP_ACK = 0x80
RESP_NACK = 0x81
RESP_STATUS = 0x82
RESP_MESSAGE = 0x83
RESP_ERROR = 0x84

HEADER_MARKER = bytes([0xAA, 0xBB, 0xCC, 0xDD])

class MelodiNode:
    """Interface to communicate with Melodi LoRa node via serial"""
    
    def __init__(self, port, baud_rate=115200):
        self.port = port
        self.baud_rate = baud_rate
        self.ser = None
        self.running = False
        
    def connect(self):
        """Connect to the serial port"""
        try:
            self.ser = serial.Serial(self.port, self.baud_rate, timeout=1)
            print(f"Connected to {self.port} at {self.baud_rate} baud")
            time.sleep(2)  # Wait for node to initialize
            return True
        except serial.SerialException as e:
            print(f"Error connecting to {self.port}: {e}")
            return False
    
    def disconnect(self):
        """Disconnect from serial port"""
        if self.ser:
            self.ser.close()
            self.ser = None
            
    def ipv6_to_bytes(self, ipv6_str):
        """Convert IPv6 string to 16 bytes"""
        try:
            return socket.inet_pton(socket.AF_INET6, ipv6_str)
        except socket.error:
            return None
            
    def bytes_to_ipv6(self, ipv6_bytes):
        """Convert 16 bytes to IPv6 string"""
        try:
            return socket.inet_ntop(socket.AF_INET6, ipv6_bytes)
        except socket.error:
            return None
    
    def send_command(self, cmd, data=b''):
        """Send command to Melodi node"""
        if not self.ser:
            return False
            
        packet = bytes([cmd]) + data
        self.ser.write(packet)
        self.ser.flush()
        return True
    
    def read_response(self, timeout=5):
        """Read response from Melodi node"""
        if not self.ser:
            return None
            
        start_time = time.time()
        buffer = b''
        
        while time.time() - start_time < timeout:
            if self.ser.in_waiting > 0:
                buffer += self.ser.read(self.ser.in_waiting)
                
                # Look for header marker
                header_pos = buffer.find(HEADER_MARKER)
                if header_pos >= 0:
                    buffer = buffer[header_pos:]
                    
                    # Check if we have at least header + response type
                    if len(buffer) >= 5:
                        response_type = buffer[4]
                        
                        # Determine expected length based on response type
                        if response_type == RESP_ACK:
                            expected_len = 5 + 1  # header + type + original_cmd
                        elif response_type == RESP_NACK:
                            expected_len = 5 + 2  # header + type + original_cmd + error
                        elif response_type == RESP_STATUS:
                            expected_len = 5 + 25  # header + type + status data
                        elif response_type == RESP_MESSAGE:
                            if len(buffer) >= 5 + 1 + 16 + 2:  # header + type + flag + src + len
                                msg_len = struct.unpack('>H', buffer[5+1+16:5+1+16+2])[0]
                                expected_len = 5 + 1 + 16 + 2 + msg_len
                            else:
                                continue
                        else:
                            expected_len = 5 + 1  # minimum
                        
                        if len(buffer) >= expected_len:
                            return buffer[:expected_len]
            
            time.sleep(0.01)
        
        return None
    
    def send_message(self, dest_ipv6, message):
        """Send message to destination IPv6 address"""
        dest_bytes = self.ipv6_to_bytes(dest_ipv6)
        if not dest_bytes:
            print(f"Invalid IPv6 address: {dest_ipv6}")
            return False
        
        msg_bytes = message.encode('utf-8')
        msg_len = len(msg_bytes)
        
        # Build command data: [2 bytes length][16 bytes dest][N bytes payload]
        data = struct.pack('>H', msg_len) + dest_bytes + msg_bytes
        
        # Send command
        if not self.send_command(CMD_SEND_MESSAGE, data):
            return False
        
        # Wait for ACK
        response = self.read_response()
        if response and len(response) >= 6:
            response_type = response[4]
            if response_type == RESP_ACK:
                return True
            elif response_type == RESP_NACK:
                error_code = response[6] if len(response) > 6 else 0
                print(f"Message send failed: error code {error_code}")
        
        print("No response to send command")
        return False
    
    def get_status(self):
        """Get node status"""
        if not self.send_command(CMD_GET_STATUS):
            return None
        
        response = self.read_response()
        if response and len(response) >= 30:  # 5 header + 25 status
            response_type = response[4]
            if response_type == RESP_STATUS:
                # Parse status response
                offset = 5  # Skip header and response type
                ipv6_bytes = response[offset:offset+16]
                radio_status = response[offset+16]
                tx_power = response[offset+17]
                freq_bytes = response[offset+18:offset+22]
                hop_limit = response[offset+22]
                uptime_bytes = response[offset+23:offset+25]
                
                ipv6_addr = self.bytes_to_ipv6(ipv6_bytes)
                frequency = struct.unpack('>I', freq_bytes)[0]
                uptime = struct.unpack('>H', uptime_bytes)[0]
                
                return {
                    'ipv6': ipv6_addr,
                    'radio_active': bool(radio_status),
                    'tx_power': tx_power,
                    'frequency': frequency,
                    'hop_limit': hop_limit,
                    'uptime': uptime
                }
        
        return None
    
    def listen_for_messages(self, callback):
        """Listen for incoming messages in a separate thread"""
        self.running = True
        
        def listen_thread():
            while self.running:
                response = self.read_response(timeout=1)
                if response and len(response) >= 5:
                    response_type = response[4]
                    if response_type == RESP_MESSAGE:
                        # Parse message response
                        offset = 5  # Skip header and response type
                        broadcast = bool(response[offset])
                        src_bytes = response[offset+1:offset+17]
                        msg_len = struct.unpack('>H', response[offset+17:offset+19])[0]
                        payload = response[offset+19:offset+19+msg_len]
                        
                        src_ipv6 = self.bytes_to_ipv6(src_bytes)
                        message = payload.decode('utf-8', errors='ignore')
                        
                        callback(src_ipv6, message, broadcast)
        
        thread = threading.Thread(target=listen_thread, daemon=True)
        thread.start()
        return thread
    
    def stop_listening(self):
        """Stop listening for messages"""
        self.running = False

def main():
    parser = argparse.ArgumentParser(description='Basic Point-to-Point Mesh Demo')
    parser.add_argument('--port', required=True, help='Serial port (e.g., /dev/ttyUSB0, COM3)')
    parser.add_argument('--dest', required=True, help='Destination IPv6 address')
    parser.add_argument('--name', default='Unknown', help='Node name for display')
    parser.add_argument('--interval', type=int, default=10, help='Message send interval (seconds)')
    parser.add_argument('--message', default=None, help='Custom message to send (default: auto-generated)')
    
    args = parser.parse_args()
    
    # Create node interface
    node = MelodiNode(args.port)
    
    if not node.connect():
        sys.exit(1)
    
    print(f"\n=== Melodi Basic P2P Demo ===")
    print(f"Node Name: {args.name}")
    print(f"Target: {args.dest}")
    print(f"Interval: {args.interval}s")
    print("=" * 40)
    
    # Get initial status
    status = node.get_status()
    if status:
        print(f"Node IPv6: {status['ipv6']}")
        print(f"Radio: {'Active' if status['radio_active'] else 'Inactive'}")
        print(f"TX Power: {status['tx_power']} dBm")
        print(f"Frequency: {status['frequency']} Hz")
        print(f"Uptime: {status['uptime']} seconds")
    else:
        print("Warning: Could not get node status")
    
    print("-" * 40)
    
    # Message received callback
    def on_message_received(src_ipv6, message, broadcast):
        timestamp = datetime.now().strftime("%H:%M:%S")
        msg_type = "BROADCAST" if broadcast else "DIRECT"
        print(f"[{timestamp}] RX {msg_type} from {src_ipv6}: {message}")
    
    # Start listening for messages
    node.listen_for_messages(on_message_received)
    
    # Main send loop
    message_count = 0
    try:
        while True:
            message_count += 1
            
            # Generate or use custom message
            if args.message:
                message = f"{args.message} #{message_count}"
            else:
                message = f"Hello from {args.name} - Message #{message_count}"
            
            timestamp = datetime.now().strftime("%H:%M:%S")
            print(f"[{timestamp}] TX to {args.dest}: {message}")
            
            # Send message
            success = node.send_message(args.dest, message)
            if success:
                print(f"[{timestamp}] ✓ Message sent successfully")
            else:
                print(f"[{timestamp}] ✗ Message send failed")
            
            # Wait for next send
            time.sleep(args.interval)
            
    except KeyboardInterrupt:
        print("\n\nShutting down...")
        node.stop_listening()
        node.disconnect()
        print("Disconnected. Goodbye!")

if __name__ == "__main__":
    main()