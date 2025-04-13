import serial
import struct


def main():
    port = "/dev/ttyACM0"
    baud_rate = 115200

    # Create a 16-byte destination IPv6 address.
    # For example: all zero bytes except the last one set to node ID 2.
    dest_addr = bytearray(16)
    dest_addr[15] = 2  # Destination node ID is 2.

    # Define your binary payload.
    # In your case this could be up to 4096 bytes.
    payload = b"Hello, this is a test message from Python!"

    # Compute total length of destination + payload.
    total_length = len(dest_addr) + len(payload)  # Must be between 16 and 4112 bytes

    # Pack total_length into 2 bytes (big-endian)
    header = struct.pack(">H", total_length)

    # Construct the complete packet.
    # Format: [Total Length (2 bytes)] [Destination (16 bytes)] [Payload (N bytes)]
    packet = header + dest_addr + payload

    # try:
    # Open the serial port.
    with serial.Serial(port, baud_rate, timeout=1) as ser:
        # Write the packet.
        ser.write(packet)
        print("Packet sent successfully!")
    # except serial.SerialException as e:
    # print("Error opening serial port:", e)


if __name__ == "__main__":
    main()
