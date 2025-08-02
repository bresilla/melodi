#!/usr/bin/env python3
"""
Interactive Mesh Terminal

This script provides an interactive command-line interface for controlling and
monitoring a Melodi LoRa mesh network node. It allows real-time message sending,
receiving, and network diagnostics.

Usage:
    python interactive_mesh.py --port /dev/ttyUSB0 --name "MyNode"
    
Commands:
    send <ipv6> <message>     - Send message to specific node
    broadcast <message>       - Send broadcast message to all nodes
    status                    - Show node status
    stats                     - Show message statistics
    nodes                     - List discovered nodes
    ping <ipv6>               - Ping specific node
    help                      - Show help
    quit/exit                 - Exit program
    
Hardware Setup:
    - Connect LoRa node to PC via USB
    - Flash the Melodi firmware to the LoRa node
"""

import serial
import struct
import socket
import time
import sys
import argparse
import threading
import json
import readline  # For command history and editing
from datetime import datetime
from collections import defaultdict

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
BROADCAST_ADDRESS = "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"

class NetworkMonitor:
    """Monitor network activity and maintain node database"""
    
    def __init__(self):
        self.discovered_nodes = {}  # ipv6 -> {first_seen, last_seen, message_count, info}
        self.message_stats = {
            'sent': 0,
            'received': 0,
            'broadcast_sent': 0,
            'broadcast_received': 0,
            'ping_sent': 0,
            'ping_responses': 0
        }
        self.pending_pings = {}  # ping_id -> {timestamp, target}
        self.ping_counter = 0
    
    def record_node_activity(self, ipv6, message_type='message'):
        """Record activity from a network node"""
        now = datetime.now()
        
        if ipv6 not in self.discovered_nodes:
            self.discovered_nodes[ipv6] = {
                'first_seen': now,
                'last_seen': now,
                'message_count': 0,
                'info': {}
            }
            print(f"\n*** NEW NODE DISCOVERED: {ipv6} ***")
        
        node = self.discovered_nodes[ipv6]
        node['last_seen'] = now
        node['message_count'] += 1
        
        if message_type == 'broadcast':
            self.message_stats['broadcast_received'] += 1
        else:
            self.message_stats['received'] += 1
    
    def record_sent_message(self, target, broadcast=False):
        """Record a sent message"""
        if broadcast:
            self.message_stats['broadcast_sent'] += 1
        else:
            self.message_stats['sent'] += 1
    
    def generate_ping_id(self):
        """Generate unique ping ID"""
        self.ping_counter += 1
        return f"PING_{self.ping_counter:04d}"
    
    def record_ping_sent(self, ping_id, target):
        """Record a ping sent"""
        self.pending_pings[ping_id] = {
            'timestamp': datetime.now(),
            'target': target
        }
        self.message_stats['ping_sent'] += 1
    
    def record_ping_response(self, src_ipv6, message_content):
        """Check if message is a ping response and record it"""
        try:
            data = json.loads(message_content)
            if data.get('type') == 'ping_response' and 'ping_id' in data:
                ping_id = data['ping_id']
                if ping_id in self.pending_pings:
                    sent_time = self.pending_pings[ping_id]['timestamp']
                    latency = (datetime.now() - sent_time).total_seconds()
                    del self.pending_pings[ping_id]
                    self.message_stats['ping_responses'] += 1
                    return latency
        except (json.JSONDecodeError, KeyError):
            pass
        return None
    
    def get_node_list(self):
        """Get list of discovered nodes"""
        return list(self.discovered_nodes.keys())
    
    def get_node_info(self, ipv6):
        """Get information about a specific node"""
        return self.discovered_nodes.get(ipv6, {})
    
    def get_stats(self):
        """Get network statistics"""
        return self.message_stats.copy()

class MelodiNode:
    """Interface to communicate with Melodi LoRa node via serial"""
    
    def __init__(self, port, baud_rate=115200):
        self.port = port
        self.baud_rate = baud_rate
        self.ser = None
        self.running = False
        self.own_ipv6 = None
        
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
            
            time.sleep(0.001)
        
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
                
                self.own_ipv6 = ipv6_addr
                
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
                response = self.read_response(timeout=0.5)
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

def print_help():
    """Print available commands"""
    print("\n=== Available Commands ===")
    print("send <ipv6> <message>     - Send message to specific node")
    print("broadcast <message>       - Send broadcast message to all nodes")
    print("ping <ipv6>              - Ping specific node")
    print("status                   - Show node status")
    print("stats                    - Show message statistics")
    print("nodes                    - List discovered nodes")
    print("clear                    - Clear screen")
    print("help                     - Show this help")
    print("quit/exit                - Exit program")
    print("========================\n")

def print_status(node):
    """Print node status"""
    status = node.get_status()
    if status:
        print(f"\n=== Node Status ===")
        print(f"IPv6 Address: {status['ipv6']}")
        print(f"Radio Status: {'Active' if status['radio_active'] else 'Inactive'}")
        print(f"TX Power: {status['tx_power']} dBm")
        print(f"Frequency: {status['frequency']} Hz")
        print(f"Hop Limit: {status['hop_limit']}")
        print(f"Uptime: {status['uptime']} seconds")
        print("==================\n")
    else:
        print("Failed to get node status\n")

def print_stats(monitor):
    """Print network statistics"""
    stats = monitor.get_stats()
    print(f"\n=== Network Statistics ===")
    print(f"Messages Sent: {stats['sent']}")
    print(f"Messages Received: {stats['received']}")
    print(f"Broadcasts Sent: {stats['broadcast_sent']}")
    print(f"Broadcasts Received: {stats['broadcast_received']}")
    print(f"Pings Sent: {stats['ping_sent']}")
    print(f"Ping Responses: {stats['ping_responses']}")
    print(f"Discovered Nodes: {len(monitor.get_node_list())}")
    print("=========================\n")

def print_nodes(monitor):
    """Print discovered nodes"""
    nodes = monitor.get_node_list()
    if not nodes:
        print("No nodes discovered yet\n")
        return
    
    print(f"\n=== Discovered Nodes ({len(nodes)}) ===")
    for ipv6 in sorted(nodes):
        info = monitor.get_node_info(ipv6)
        last_seen = info['last_seen'].strftime("%H:%M:%S")
        print(f"{ipv6} - {info['message_count']} msgs, last seen: {last_seen}")
    print("===============================\n")

def main():
    parser = argparse.ArgumentParser(description='Interactive Mesh Terminal')
    parser.add_argument('--port', required=True, help='Serial port (e.g., /dev/ttyUSB0)')
    parser.add_argument('--name', default='InteractiveNode', help='Node name for identification')
    
    args = parser.parse_args()
    
    # Create node interface and monitor
    node = MelodiNode(args.port)
    monitor = NetworkMonitor()
    
    if not node.connect():
        sys.exit(1)
    
    print(f"\n=== Melodi Interactive Mesh Terminal ===")
    print(f"Node Name: {args.name}")
    print(f"Port: {args.port}")
    print("========================================")
    
    # Get initial status
    print("Initializing...")
    status = node.get_status()
    if status:
        print(f"Connected as: {status['ipv6']}")
        print(f"Radio: {'Active' if status['radio_active'] else 'Inactive'}")
    else:
        print("Warning: Could not get initial status")
    
    print("\nType 'help' for available commands")
    print("Listening for mesh traffic...\n")
    
    # Message received callback
    def on_message_received(src_ipv6, message, broadcast):
        timestamp = datetime.now().strftime("%H:%M:%S")
        msg_type = "BROADCAST" if broadcast else "DIRECT"
        
        # Record node activity
        monitor.record_node_activity(src_ipv6, 'broadcast' if broadcast else 'message')
        
        # Check if it's a ping response
        latency = monitor.record_ping_response(src_ipv6, message)
        if latency is not None:
            print(f"\n[{timestamp}] PING RESPONSE from {src_ipv6}: {latency:.3f}s")
        else:
            # Try to parse as structured message
            try:
                data = json.loads(message)
                if data.get('type') == 'ping':
                    # Respond to ping
                    ping_id = data.get('ping_id', 'unknown')
                    response = json.dumps({
                        'type': 'ping_response',
                        'ping_id': ping_id,
                        'responder': args.name,
                        'timestamp': datetime.now().isoformat()
                    })
                    node.send_message(src_ipv6, response)
                    print(f"\n[{timestamp}] PING from {src_ipv6} - responded")
                else:
                    print(f"\n[{timestamp}] RX {msg_type} from {src_ipv6}: {message}")
            except json.JSONDecodeError:
                # Regular text message
                print(f"\n[{timestamp}] RX {msg_type} from {src_ipv6}: {message}")
        
        # Show prompt again
        print("> ", end="", flush=True)
    
    # Start listening
    node.listen_for_messages(on_message_received)
    
    # Interactive command loop
    try:
        while True:
            try:
                command = input("> ").strip()
                if not command:
                    continue
                
                parts = command.split(None, 2)
                cmd = parts[0].lower()
                
                if cmd in ['quit', 'exit']:
                    break
                elif cmd == 'help':
                    print_help()
                elif cmd == 'status':
                    print_status(node)
                elif cmd == 'stats':
                    print_stats(monitor)
                elif cmd == 'nodes':
                    print_nodes(monitor)
                elif cmd == 'clear':
                    print("\033[2J\033[H", end="")  # Clear screen
                elif cmd == 'send' and len(parts) >= 3:
                    target = parts[1]
                    message = parts[2]
                    print(f"Sending to {target}: {message}")
                    if node.send_message(target, message):
                        monitor.record_sent_message(target)
                        print("Message sent successfully")
                    else:
                        print("Message send failed")
                elif cmd == 'broadcast' and len(parts) >= 2:
                    message = parts[1]
                    print(f"Broadcasting: {message}")
                    if node.send_message(BROADCAST_ADDRESS, message):
                        monitor.record_sent_message(BROADCAST_ADDRESS, broadcast=True)
                        print("Broadcast sent successfully")
                    else:
                        print("Broadcast send failed")
                elif cmd == 'ping' and len(parts) >= 2:
                    target = parts[1]
                    ping_id = monitor.generate_ping_id()
                    ping_msg = json.dumps({
                        'type': 'ping',
                        'ping_id': ping_id,
                        'sender': args.name,
                        'timestamp': datetime.now().isoformat()
                    })
                    print(f"Pinging {target}...")
                    if node.send_message(target, ping_msg):
                        monitor.record_ping_sent(ping_id, target)
                        print("Ping sent")
                    else:
                        print("Ping send failed")
                else:
                    print("Unknown command or invalid syntax. Type 'help' for available commands.")
                
            except EOFError:
                break
            except KeyboardInterrupt:
                print("\nUse 'quit' or 'exit' to close the terminal")
                
    except KeyboardInterrupt:
        pass
    
    print("\nShutting down...")
    print_stats(monitor)
    
    node.stop_listening()
    node.disconnect()
    print("Terminal closed. Goodbye!")

if __name__ == "__main__":
    main()