import serial
import struct
import argparse
import socket


def main():
    parser = argparse.ArgumentParser(
        description="Send a binary packet via a serial connection with a custom string payload."
    )
    parser.add_argument(
        "--port", required=True, help="Serial port to use (e.g., /dev/ttyACM0 or COM3)"
    )
    parser.add_argument(
        "--address", required=True, help="Destination IPv6 address (e.g., 2001:db8::1)"
    )
    parser.add_argument(
        "--payload", required=True, help="Content of the payload as a string."
    )
    args = parser.parse_args()

    port = args.port
    baud_rate = 115200

    # Convert the provided IPv6 address into a 16-byte binary representation.
    try:
        dest_addr = socket.inet_pton(socket.AF_INET6, args.address)
    except socket.error as e:
        print(f"Error: Invalid IPv6 address '{args.address}'.")
        return

    print(f"Destination IPv6 address: {args.address}")

    # Convert the provided payload string to bytes using UTF-8 encoding.
    payload_bytes = args.payload.encode("utf-8")

    # Pack the payload length into 2 bytes in big-endian format.
    header = struct.pack(">H", len(payload_bytes))
    print(f"Payload length: {len(payload_bytes)}")

    # Construct the packet: [Payload Length (2 bytes)] [Destination (16 bytes)] [Payload (N bytes)]
    packet = header + dest_addr + payload_bytes

    try:
        # Open the serial port and send the packet.
        with serial.Serial(port, baud_rate, timeout=1) as ser:
            ser.write(packet)
            print("Packet sent successfully!")
    except serial.SerialException as e:
        print("Error opening/writing to serial port:", e)


if __name__ == "__main__":
    main()
