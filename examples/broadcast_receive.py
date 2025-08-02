#!/usr/bin/env python3
"""
Broadcast Mesh Receiver

This script demonstrates receiving broadcast messages from the Melodi LoRa mesh network.
The PC listens for messages via serial from the connected LoRa node, which receives
transmissions from all nodes in the mesh network.

Usage:
    python broadcast_receive.py --port /dev/ttyUSB0 --name "Receiver-Node"
    
Hardware Setup:
    - Connect LoRa node (TTGO/Feather/etc.) to PC via USB
    - Flash the Melodi firmware to the LoRa node
    - Run this script to monitor mesh traffic
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
CMD_GET_STATUS = 0x03

RESP_ACK = 0x80
RESP_NACK = 0x81
RESP_STATUS = 0x82
RESP_MESSAGE = 0x83
RESP_ERROR = 0x84

HEADER_MARKER = bytes([0xAA, 0xBB, 0xCC, 0xDD])

class MelodiReceiver:
    """Interface to receive messages from Melodi LoRa node via serial"""
    
    def __init__(self, port, baud_rate=115200):
        self.port = port
        self.baud_rate = baud_rate
        self.ser = None
        self.running = False
        self.message_stats = {
            'received': 0,
            'broadcast_received': 0,
            'direct_received': 0
        }
        
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
                            expected_len = 5 + 1
                        elif response_type == RESP_NACK:
                            expected_len = 5 + 2
                        elif response_type == RESP_STATUS:
                            expected_len = 5 + 25
                        elif response_type == RESP_MESSAGE:
                            if len(buffer) >= 5 + 1 + 16 + 2:
                                msg_len = struct.unpack('>H', buffer[5+1+16:5+1+16+2])[0]
                                expected_len = 5 + 1 + 16 + 2 + msg_len
                            else:
                                continue
                        else:
                            expected_len = 5 + 1
                        
                        if len(buffer) >= expected_len:
                            return buffer[:expected_len]
            
            time.sleep(0.01)
        
        return None
    
    def get_status(self):
        """Get node status"""
        if not self.send_command(CMD_GET_STATUS):
            return None
        
        response = self.read_response()
        if response and len(response) >= 30:
            response_type = response[4]
            if response_type == RESP_STATUS:
                offset = 5
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
                        offset = 5
                        broadcast = bool(response[offset])
                        src_bytes = response[offset+1:offset+17]
                        msg_len = struct.unpack('>H', response[offset+17:offset+19])[0]
                        payload = response[offset+19:offset+19+msg_len]
                        
                        src_ipv6 = self.bytes_to_ipv6(src_bytes)
                        message = payload.decode('utf-8', errors='ignore')
                        
                        # Update stats
                        self.message_stats['received'] += 1
                        if broadcast:
                            self.message_stats['broadcast_received'] += 1
                        else:
                            self.message_stats['direct_received'] += 1
                        
                        callback(src_ipv6, message, broadcast)
        
        thread = threading.Thread(target=listen_thread, daemon=True)
        thread.start()
        return thread
    
    def stop_listening(self):
        """Stop listening for messages"""
        self.running = False
    
    def get_stats(self):
        """Get message statistics"""
        return self.message_stats.copy()

def print_stats(receiver, node_name):
    """Print current statistics"""
    stats = receiver.get_stats()
    print(f"\n--- {node_name} Statistics ---")
    print(f"Messages Received: {stats['received']}")
    print(f"  └─ Broadcast: {stats['broadcast_received']}")
    print(f"  └─ Direct: {stats['direct_received']}")
    print("-" * 30)

def main():
    parser = argparse.ArgumentParser(description='Broadcast Mesh Receiver')
    parser.add_argument('--port', required=True, help='Serial port (e.g., /dev/ttyUSB0, COM3)')
    parser.add_argument('--name', default='Receiver', help='Node name for display')
    parser.add_argument('--stats-interval', type=int, default=60, help='Stats display interval (seconds)')
    parser.add_argument('--verbose', action='store_true', help='Show detailed message information')
    
    args = parser.parse_args()
    
    # Create receiver interface
    receiver = MelodiReceiver(args.port)
    
    if not receiver.connect():
        sys.exit(1)
    
    print(f"\n=== Melodi Broadcast Receiver ===")
    print(f"Node Name: {args.name}")
    print(f"Mode: RECEIVE ONLY")
    print("=" * 40)
    
    # Get initial status
    status = receiver.get_status()
    if status:
        print(f"Node IPv6: {status['ipv6']}")
        print(f"Radio: {'Active' if status['radio_active'] else 'Inactive'}")
        print(f"TX Power: {status['tx_power']} dBm")
        print(f"Frequency: {status['frequency']} Hz")
        print(f"Hop Limit: {status['hop_limit']}")
        print(f"Uptime: {status['uptime']} seconds")
    else:
        print("Warning: Could not get node status")
    
    print("-" * 40)
    print("Listening for mesh traffic...")
    print("Press Ctrl+C to stop")
    print("-" * 40)
    
    # Track unique sources and message history
    seen_sources = set()
    message_history = []
    
    # Message received callback
    def on_message_received(src_ipv6, message, broadcast):
        timestamp = datetime.now().strftime("%H:%M:%S")
        msg_type = "BROADCAST" if broadcast else "DIRECT"
        
        # Track new sources
        if src_ipv6 not in seen_sources:
            seen_sources.add(src_ipv6)
            print(f"[{timestamp}] *** NEW NODE DISCOVERED: {src_ipv6} ***")
        
        # Store message in history
        message_entry = {
            'timestamp': timestamp,
            'src': src_ipv6,
            'message': message,
            'broadcast': broadcast
        }
        message_history.append(message_entry)
        
        # Keep only last 100 messages
        if len(message_history) > 100:
            message_history.pop(0)
        
        # Display message
        print(f"[{timestamp}] RX {msg_type} from {src_ipv6}: {message}")
        
        if args.verbose:
            print(f"    └─ Message Length: {len(message)} bytes")
            print(f"    └─ Source: {src_ipv6}")
            print(f"    └─ Type: {msg_type}")
    
    # Start listening for messages
    receiver.listen_for_messages(on_message_received)
    
    # Stats display loop
    last_stats_time = time.time()
    
    try:
        while True:
            time.sleep(1)
            
            # Display stats periodically
            current_time = time.time()
            if current_time - last_stats_time >= args.stats_interval:
                print_stats(receiver, args.name)
                print(f"Discovered Nodes: {len(seen_sources)}")
                if seen_sources:
                    print("Known Sources:")
                    for i, src in enumerate(sorted(seen_sources), 1):
                        print(f"  {i}. {src}")
                
                if args.verbose and message_history:
                    print(f"Recent Messages: {len(message_history)}")
                    for msg in message_history[-5:]:  # Show last 5 messages
                        msg_type = "BROADCAST" if msg['broadcast'] else "DIRECT"
                        print(f"  [{msg['timestamp']}] {msg_type}: {msg['message'][:50]}...")
                
                print("-" * 40)
                last_stats_time = current_time
            
    except KeyboardInterrupt:
        print("\n\nShutting down...")
        print_stats(receiver, args.name)
        print(f"Total Discovered Nodes: {len(seen_sources)}")
        print(f"Total Messages Received: {len(message_history)}")
        
        if seen_sources:
            print("Final Node List:")
            for i, src in enumerate(sorted(seen_sources), 1):
                print(f"  {i}. {src}")
        
        if args.verbose and message_history:
            print("\nMessage Summary:")
            broadcast_count = sum(1 for msg in message_history if msg['broadcast'])
            direct_count = len(message_history) - broadcast_count
            print(f"  Broadcast Messages: {broadcast_count}")
            print(f"  Direct Messages: {direct_count}")
        
        receiver.stop_listening()
        receiver.disconnect()
        print("Disconnected. Goodbye!")

if __name__ == "__main__":
    main()