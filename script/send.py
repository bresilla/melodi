import serial
import struct
import argparse
import socket


def main():
    parser = argparse.ArgumentParser(
        description="Send a binary packet via a serial connection."
    )
    parser.add_argument(
        "--port", required=True, help="Serial port to use (e.g., /dev/ttyACM0 or COM3)"
    )
    parser.add_argument(
        "--address", required=True, help="Destination IPv6 address (e.g., 2001:db8::1)"
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

    # Define the binary payload.
    payload = b"Hello, this is a test message from Python!"

    # Compute the total length which is the sum of the destination address bytes and payload bytes.
    # Note: The total length must be between 16 and 4112 bytes.
    total_length = len(dest_addr) + len(payload)

    # Pack the total_length into 2 bytes in big-endian format.
    header = struct.pack(">H", total_length)

    # Construct the packet: [Total Length (2 bytes)] [Destination (16 bytes)] [Payload (N bytes)]
    packet = header + dest_addr + payload

    try:
        # Open and write to the serial port.
        with serial.Serial(port, baud_rate, timeout=1) as ser:
            ser.write(packet)
            print("Packet sent successfully!")
    except serial.SerialException as e:
        print("Error opening/writing to serial port:", e)


if __name__ == "__main__":
    main()
