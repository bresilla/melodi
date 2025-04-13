import os
import socket

Import("env")

board_serial = os.environ.get("BOARD_SERIAL")
ipv6 = os.environ.get("BOARD_IPV6")

if ipv6:
    try:
        ipv6_correct = socket.inet_pton(socket.AF_INET6, ipv6)
        ipv6_first = int.from_bytes(ipv6_correct[:8], byteorder="big")
        ipv6_last = int.from_bytes(ipv6_correct[8:], byteorder="big")
    except socket.error as e:
        print("Error converting IPv6 to int:", e)
elif board_serial:
    if len(board_serial) > 16:
        board_serial = board_serial[-16:]
    try:
        ipv6_last = int(board_serial, 16)
        ipv6_first = 0xFFFF
    except ValueError as e:
        print("Error converting serial to int:", e)
else:
    ipv6_first = 0xFFFF
    ipv6_last = 0xFFFF

# Convert back to IPv6 address format
ipv6_first_bytes = ipv6_first.to_bytes(8, byteorder="big")
ipv6_last_bytes = ipv6_last.to_bytes(8, byteorder="big")
ipv6_address = socket.inet_ntop(socket.AF_INET6, ipv6_first_bytes + ipv6_last_bytes)

print("-------------------- IPV6 --------------------")
print("    %s" % ipv6_address)
print("-------------------- IPV6 --------------------")

env.Append(CPPDEFINES=["IPV6_FIRST=0x%x" % ipv6_first, "IPV6_LAST=0x%x" % ipv6_last])
