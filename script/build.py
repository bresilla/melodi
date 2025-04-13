import os

Import("env")

ipv6_first = 0x00
ipv6_last = 0x00


board_serial = os.environ.get("BOARD_SERIAL")

if board_serial:
    print("----------------------------------------------------")
    if len(board_serial) > 16:
        board_serial = board_serial[-16:]
    try:
        ipv6_last = int(board_serial, 16)
    except ValueError as e:
        print("Error converting serial to int:", e)


env.Append(CPPDEFINES=["IPV6_FIRST=0x%x" % ipv6_first, "IPV6_LAST=0x%x" % ipv6_last])
