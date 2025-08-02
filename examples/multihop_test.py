#!/usr/bin/env python3
"""
Multi-hop Routing Test Tool

This script tests multi-hop routing in the Melodi LoRa mesh network by sending
messages with hop tracing and measuring end-to-end latency. It helps analyze
mesh routing behavior and network performance.

Usage:
    python multihop_test.py --port /dev/ttyUSB0 --target 2001:db8::5 --name "TestNode-A"
    
Hardware Setup:
    - Connect LoRa node to PC via USB
    - Deploy multiple nodes across the intended coverage area
    - Use different hop limits to test routing behavior
"""

import serial
import struct
import socket
import time
import sys
import argparse
import threading
import json
from datetime import datetime, timedelta

# Protocol constants
CMD_SEND_MESSAGE = 0x01
CMD_SET_IPV6 = 0x02
CMD_GET_STATUS = 0x03

RESP_ACK = 0x80
RESP_NACK = 0x81
RESP_STATUS = 0x82
RESP_MESSAGE = 0x83
RESP_ERROR = 0x84

HEADER_MARKER = bytes([0xAA, 0xBB, 0xCC, 0xDD])

class RouteTracer:
    """Track routing performance and statistics"""
    
    def __init__(self):
        self.sent_messages = {}  # message_id -> {timestamp, target, content}
        self.received_messages = []  # list of received message info
        self.route_stats = {}  # target -> {success_count, total_attempts, avg_latency}
        self.message_counter = 0
    
    def generate_test_message(self, target, test_type="ping"):
        """Generate a test message with unique ID"""
        self.message_counter += 1
        message_id = f"MSG_{self.message_counter:04d}"
        timestamp = datetime.now()
        
        message = {
            "id": message_id,
            "type": test_type,
            "timestamp": timestamp.isoformat(),
            "target": target,
            "source": "test_node"
        }
        
        # Store for tracking
        self.sent_messages[message_id] = {
            'timestamp': timestamp,
            'target': target,
            'content': message
        }
        
        return message_id, json.dumps(message)
    
    def record_received_message(self, src_ipv6, message_content, broadcast):
        """Record a received message for analysis"""
        try:
            message = json.loads(message_content)
            if 'id' in message and 'timestamp' in message:
                receive_time = datetime.now()
                
                # Calculate latency if this was one of our sent messages
                message_id = message['id']
                if message_id in self.sent_messages:
                    sent_info = self.sent_messages[message_id]
                    latency = (receive_time - sent_info['timestamp']).total_seconds()
                    
                    # Update route stats
                    target = sent_info['target']
                    if target not in self.route_stats:
                        self.route_stats[target] = {
                            'success_count': 0,
                            'total_attempts': 0,
                            'latencies': [],
                            'avg_latency': 0
                        }
                    
                    self.route_stats[target]['success_count'] += 1
                    self.route_stats[target]['latencies'].append(latency)
                    self.route_stats[target]['avg_latency'] = sum(self.route_stats[target]['latencies']) / len(self.route_stats[target]['latencies'])
                
                self.received_messages.append({
                    'id': message_id,
                    'src': src_ipv6,
                    'content': message,
                    'broadcast': broadcast,
                    'receive_time': receive_time,
                    'latency': latency if 'latency' in locals() else None
                })
                
                return True
        except (json.JSONDecodeError, KeyError):
            # Not a test message, ignore
            pass
        
        return False
    
    def record_send_attempt(self, target):
        """Record a send attempt for statistics"""
        if target not in self.route_stats:
            self.route_stats[target] = {
                'success_count': 0,
                'total_attempts': 0,
                'latencies': [],
                'avg_latency': 0
            }
        self.route_stats[target]['total_attempts'] += 1
    
    def get_route_stats(self, target=None):
        """Get routing statistics"""
        if target:
            return self.route_stats.get(target, {})
        return self.route_stats

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
            time.sleep(2)
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
                
                header_pos = buffer.find(HEADER_MARKER)
                if header_pos >= 0:
                    buffer = buffer[header_pos:]
                    
                    if len(buffer) >= 5:
                        response_type = buffer[4]
                        
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
    
    def send_message(self, dest_ipv6, message):
        """Send message to destination IPv6 address"""
        dest_bytes = self.ipv6_to_bytes(dest_ipv6)
        if not dest_bytes:
            return False
        
        msg_bytes = message.encode('utf-8')
        msg_len = len(msg_bytes)
        
        data = struct.pack('>H', msg_len) + dest_bytes + msg_bytes
        
        if not self.send_command(CMD_SEND_MESSAGE, data):
            return False
        
        response = self.read_response()
        if response and len(response) >= 6:
            response_type = response[4]
            return response_type == RESP_ACK
        
        return False
    
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
                        offset = 5
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

def print_route_stats(tracer, target=None):
    """Print routing statistics"""
    stats = tracer.get_route_stats(target)
    
    if target:
        if target in stats:
            s = stats[target]
            success_rate = (s['success_count'] / s['total_attempts'] * 100) if s['total_attempts'] > 0 else 0
            print(f"\n--- Route Stats for {target} ---")
            print(f"Success Rate: {success_rate:.1f}% ({s['success_count']}/{s['total_attempts']})")
            print(f"Average Latency: {s['avg_latency']:.2f}s")
            if s['latencies']:
                print(f"Min/Max Latency: {min(s['latencies']):.2f}s / {max(s['latencies']):.2f}s")
        else:
            print(f"\nNo statistics available for {target}")
    else:
        print("\n--- Overall Route Statistics ---")
        for target, s in stats.items():
            success_rate = (s['success_count'] / s['total_attempts'] * 100) if s['total_attempts'] > 0 else 0
            print(f"{target}: {success_rate:.1f}% success, {s['avg_latency']:.2f}s avg latency")

def main():
    parser = argparse.ArgumentParser(description='Multi-hop Routing Test Tool')
    parser.add_argument('--port', required=True, help='Serial port (e.g., /dev/ttyUSB0)')
    parser.add_argument('--target', required=True, help='Target IPv6 address to test')
    parser.add_argument('--name', default='TestNode', help='Node name for identification')
    parser.add_argument('--interval', type=int, default=10, help='Test message interval (seconds)')
    parser.add_argument('--count', type=int, default=0, help='Number of test messages (0 = infinite)')
    parser.add_argument('--timeout', type=int, default=30, help='Response timeout (seconds)')
    parser.add_argument('--stats-interval', type=int, default=60, help='Statistics display interval')
    
    args = parser.parse_args()
    
    # Create node interface
    node = MelodiNode(args.port)
    
    if not node.connect():
        sys.exit(1)
    
    # Create route tracer
    tracer = RouteTracer()
    
    print(f"\n=== Melodi Multi-hop Test Tool ===")
    print(f"Test Node: {args.name}")
    print(f"Target: {args.target}")
    print(f"Interval: {args.interval}s")
    print(f"Max Messages: {'Unlimited' if args.count == 0 else args.count}")
    print("=" * 50)
    
    # Get initial status
    status = node.get_status()
    if status:
        print(f"Node IPv6: {status['ipv6']}")
        print(f"Radio: {'Active' if status['radio_active'] else 'Inactive'}")
        print(f"TX Power: {status['tx_power']} dBm")
        print(f"Hop Limit: {status['hop_limit']}")
        print(f"Frequency: {status['frequency']} Hz")
    else:
        print("Warning: Could not get node status")
    
    print("-" * 50)
    
    # Message received callback
    def on_message_received(src_ipv6, message, broadcast):
        timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        
        # Try to process as test message
        if tracer.record_received_message(src_ipv6, message, broadcast):
            # Get the latest received message
            latest = tracer.received_messages[-1]
            if latest['latency'] is not None:
                print(f"[{timestamp}] ✓ RESPONSE from {src_ipv6}: {latest['id']} (latency: {latest['latency']:.3f}s)")
            else:
                print(f"[{timestamp}] RX from {src_ipv6}: {latest['id']}")
        else:
            # Regular message
            msg_type = "BROADCAST" if broadcast else "DIRECT"
            print(f"[{timestamp}] RX {msg_type} from {src_ipv6}: {message}")
    
    # Start listening
    node.listen_for_messages(on_message_received)
    
    # Main test loop
    messages_sent = 0
    last_stats_time = time.time()
    
    try:
        while True:
            if args.count > 0 and messages_sent >= args.count:
                print(f"\nCompleted {args.count} test messages")
                break
            
            # Generate test message
            message_id, message_content = tracer.generate_test_message(args.target, "multihop_test")
            
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            print(f"[{timestamp}] TX to {args.target}: {message_id}")
            
            # Send message
            tracer.record_send_attempt(args.target)
            success = node.send_message(args.target, message_content)
            
            if success:
                messages_sent += 1
                print(f"[{timestamp}] ✓ Message sent successfully")
            else:
                print(f"[{timestamp}] ✗ Message send failed")
            
            # Display stats periodically
            current_time = time.time()
            if current_time - last_stats_time >= args.stats_interval:
                print_route_stats(tracer, args.target)
                print("-" * 50)
                last_stats_time = current_time
            
            # Wait for next test
            time.sleep(args.interval)
            
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
    
    print("\n=== Final Test Results ===")
    print_route_stats(tracer, args.target)
    print(f"Total Messages Sent: {messages_sent}")
    print(f"Total Responses Received: {len(tracer.received_messages)}")
    
    node.stop_listening()
    node.disconnect()
    print("Disconnected. Test complete!")

if __name__ == "__main__":
    main()