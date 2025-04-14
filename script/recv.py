import serial
import struct

# ----- Configuration Constants -----
HEADER_MARKER = b"\xaa\xbb\xcc\xdd"
IPV6_ADDR_LEN = 16
MAX_PAYLOAD_SIZE = 16 * 8  # e.g., 128 bytes
MAX_FRAGMENTS = 16
MAX_MESSAGE_SIZE = MAX_FRAGMENTS * MAX_PAYLOAD_SIZE  # e.g., 2048 bytes
PACKET_SIZE = 1 + IPV6_ADDR_LEN + 2 + MAX_MESSAGE_SIZE  # Total ReassembledPacket size


# ----- Functions -----
def read_packet(ser):
    """
    Reads from the serial port until the HEADER_MARKER is found,
    then reads the next PACKET_SIZE bytes that constitute our binary packet.
    """
    buffer = b""
    marker_len = len(HEADER_MARKER)

    while True:
        # Read one byte at a time
        byte = ser.read(1)
        if not byte:
            continue
        buffer += byte
        # Only need to check the tail end of the buffer
        if len(buffer) > marker_len:
            buffer = buffer[-marker_len:]

        if buffer == HEADER_MARKER:
            # Marker detected: read the fixed number of bytes for the packet
            packet_data = ser.read(PACKET_SIZE)
            if len(packet_data) < PACKET_SIZE:
                print("Incomplete packet received.")
                return None
            return packet_data


def main():
    port = input("Enter serial port (e.g., COM3 or /dev/ttyACM0): ")
    baud_rate = 115200

    try:
        ser = serial.Serial(port, baud_rate, timeout=1)
    except serial.SerialException as e:
        print("Error opening serial port:", e)
        return

    print(f"Listening on {port}...")

    while True:
        packet_data = read_packet(ser)
        if packet_data is None:
            continue

        # ----- Unpack the binary packet -----
        # Format string explanation:
        #   - '?'       : 1 byte boolean for broadcast
        #   - '16s'     : 16 bytes for the source IPv6 address
        #   - 'H'       : 2 bytes unsigned short for payloadLength (big-endian if needed; adjust with '>' if required)
        #   - f'{MAX_MESSAGE_SIZE}s' : MAX_MESSAGE_SIZE bytes for payload
        fmt = f"?{IPV6_ADDR_LEN}sH{MAX_MESSAGE_SIZE}s"
        try:
            unpacked = struct.unpack(fmt, packet_data)
        except struct.error as e:
            print("Error unpacking binary data:", e)
            continue

        broadcast = unpacked[0]
        source = unpacked[1]
        payloadLength = unpacked[2]
        payload = unpacked[3][:payloadLength]  # Only take the valid payload

        # For display, convert source to hexadecimal representation
        source_hex = ":".join(f"{b:02X}" for b in source)

        print("Received Packet:")
        print("  Broadcast:    ", broadcast)
        print("  Source:       ", source_hex)
        print("  Payload Len:  ", payloadLength)
        print("  Payload Data: ", payload)
        print("--------------")


if __name__ == "__main__":
    main()
